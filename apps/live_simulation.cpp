#include "live_simulation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>

#include "chiller/model_factory.hpp"

namespace chiller {

using nlohmann::json;

namespace {

double round3(double v) { return std::isfinite(v) ? std::round(v * 1000.0) / 1000.0 : 0.0; }

json recordToJson(const BlackBoxRecord& r) {
    json extras = json::object();
    try {
        extras = json::parse(r.extrasJson);
        for (auto& [key, value] : extras.items()) {
            if (value.is_number()) value = round3(value.get<double>());
        }
    } catch (const json::exception&) {
        // keep empty extras if the stored text is not valid JSON
    }
    return json{{"time_s", r.timeS},
                {"model_id", r.modelId},
                {"model_type", r.modelType},
                {"setpoint_c", round3(r.setpointC)},
                {"load_pct", round3(r.loadPct)},
                {"chw_supply_c", round3(r.chwSupplyC)},
                {"chw_return_c", round3(r.chwReturnC)},
                {"flow_pct", round3(r.flowPct)},
                {"cond_pressure_kpa", round3(r.condPressureKpa)},
                {"power_kw", round3(r.powerKw)},
                {"capacity_request_pct", round3(r.capacityRequestPct)},
                {"capacity_command_pct", round3(r.capacityCommandPct)},
                {"capacity_actual_pct", round3(r.capacityActualPct)},
                {"state", r.state},
                {"fault_code", r.faultCode},
                {"native_fault_code", r.nativeFaultCode},
                {"extras", extras}};
}

}  // namespace

LiveSimulation::LiveSimulation(LiveOptions options) : opt_(std::move(options)) {
    namespace fs = std::filesystem;
    if (opt_.scenarioPath.empty()) opt_.scenarioPath = (fs::path(opt_.configDir) / "scenario.json").string();
    scenario_ = loadScenario(opt_.scenarioPath);
    configs_ = loadAllChillerConfigs((fs::path(opt_.configDir) / "models").string());
    for (const auto& cfg : configs_) models_.push_back(makeChiller(cfg, scenario_.seed));
    logger_ = std::make_unique<BlackBoxLogger>(opt_.dbPath, 50);
    loadPct_ = scenario_.loadPctAt(0.0);
    speed_ = std::clamp(opt_.speed, 0.5, 50.0);
    std::lock_guard<std::mutex> lock(mu_);
    startRunLocked();
}

LiveSimulation::~LiveSimulation() { stop(); }

void LiveSimulation::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { loop(); });
}

void LiveSimulation::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    logger_->flush();
}

void LiveSimulation::startRunLocked() {
    ChillerModel& model = *models_[active_];
    model.reset();
    app_ = std::make_unique<ControlApp>(model, scenario_.pid, app_ ? app_->setpoint() : scenario_.setpointC);
    history_.clear();
    injectedNames_.clear();
    logger_->beginRun(model.config().modelId, model.type(), "live", "dashboard session");
}

void LiveSimulation::loop() {
    using clock = std::chrono::steady_clock;
    auto next = clock::now();
    while (running_) {
        double speed;
        {
            std::lock_guard<std::mutex> lock(mu_);
            tickLocked();
            speed = speed_;
        }
        next += std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(scenario_.dtS / speed));
        const auto now = clock::now();
        if (next < now - std::chrono::seconds(1)) next = now;  // don't try to catch up after a long pause
        std::this_thread::sleep_until(next);
    }
}

void LiveSimulation::tickLocked() {
    ChillerModel& model = *models_[active_];
    const double t = model.simTimeS();
    TickResult tick = app_->tick(loadPct_, scenario_.dtS);
    for (const DetectedFault& f : tick.newFaults) logger_->logFaultEvent(t, model.config().modelId, f, "RAISED");
    logger_->write(tick.record);
    history_.push_back(std::move(tick.record));
    while (history_.size() > opt_.historyLimit) history_.pop_front();
}

json LiveSimulation::models() const {
    std::lock_guard<std::mutex> lock(mu_);
    json list = json::array();
    for (std::size_t i = 0; i < configs_.size(); ++i) {
        json m = configs_[i].toJson();
        m["active"] = (i == active_);
        list.push_back(m);
    }
    return list;
}

json LiveSimulation::stateLocked() const {
    const ChillerModel& model = *models_[active_];
    json faults = json::array();
    for (const DetectedFault& f : app_->activeFaults()) {
        faults.push_back({{"code", toString(f.code)},
                          {"native_code", f.nativeCode},
                          {"name", describe(f.code)},
                          {"description", f.description}});
    }
    json injectable = json::array();
    for (InjectedFault f : allInjectableFaults()) injectable.push_back(toString(f));
    const PidGains g = scenario_.pid;
    return json{{"time_s", model.simTimeS()},
                {"model", model.config().toJson()},
                {"setpoint_c", app_->setpoint()},
                {"load_pct", loadPct_},
                {"speed", speed_},
                {"state", toString(model.state())},
                {"compressor_starts", model.compressorStarts()},
                {"run_id", logger_->currentRunId()},
                {"active_faults", faults},
                {"injected", injectedNames_},
                {"injectable_faults", injectable},
                {"pid", {{"kp", g.kp}, {"ki", g.ki}, {"kd", g.kd}, {"actuator_feedback", g.actuatorFeedback}}},
                {"latest", history_.empty() ? json(nullptr) : recordToJson(history_.back())}};
}

json LiveSimulation::state() const {
    std::lock_guard<std::mutex> lock(mu_);
    return stateLocked();
}

json LiveSimulation::history(double seconds) const {
    std::lock_guard<std::mutex> lock(mu_);
    json h = {{"time_s", json::array()},         {"chw_supply_c", json::array()},
              {"chw_return_c", json::array()},   {"setpoint_c", json::array()},
              {"capacity_request_pct", json::array()}, {"capacity_command_pct", json::array()},
              {"capacity_actual_pct", json::array()},  {"power_kw", json::array()},
              {"load_pct", json::array()}};
    if (history_.empty()) return h;
    const double from = history_.back().timeS - seconds;
    for (const BlackBoxRecord& r : history_) {
        if (r.timeS < from) continue;
        h["time_s"].push_back(r.timeS);
        h["chw_supply_c"].push_back(round3(r.chwSupplyC));
        h["chw_return_c"].push_back(round3(r.chwReturnC));
        h["setpoint_c"].push_back(round3(r.setpointC));
        h["capacity_request_pct"].push_back(round3(r.capacityRequestPct));
        h["capacity_command_pct"].push_back(round3(r.capacityCommandPct));
        h["capacity_actual_pct"].push_back(round3(r.capacityActualPct));
        h["power_kw"].push_back(round3(r.powerKw));
        h["load_pct"].push_back(round3(r.loadPct));
    }
    return h;
}

json LiveSimulation::faults(int limit) const {
    json list = json::array();
    for (const FaultEvent& e : logger_->recentFaultEvents(std::clamp(limit, 1, 500))) {
        list.push_back({{"id", e.id},
                        {"run_id", e.runId},
                        {"time_s", e.timeS},
                        {"model_id", e.modelId},
                        {"fault_code", e.faultCode},
                        {"native_code", e.nativeCode},
                        {"description", e.description},
                        {"event", e.event},
                        {"wall_clock", e.wallClock}});
    }
    return list;
}

json LiveSimulation::runs(int limit) const {
    json list = json::array();
    for (const RunInfo& r : logger_->recentRuns(std::clamp(limit, 1, 200))) {
        list.push_back({{"run_id", r.runId},
                        {"started_at", r.startedAt},
                        {"model_id", r.modelId},
                        {"model_type", r.modelType},
                        {"scenario", r.scenario},
                        {"notes", r.notes},
                        {"records", r.records}});
    }
    return list;
}

std::string LiveSimulation::selectModel(const std::string& modelId) {
    std::lock_guard<std::mutex> lock(mu_);
    for (std::size_t i = 0; i < configs_.size(); ++i) {
        if (configs_[i].modelId == modelId || configs_[i].type == modelId) {
            active_ = i;
            startRunLocked();
            return "";
        }
    }
    return "unknown model '" + modelId + "'";
}

std::string LiveSimulation::setSetpoint(double celsius) {
    if (!std::isfinite(celsius) || celsius < 4.0 || celsius > 15.0) return "setpoint must be between 4 and 15 C";
    std::lock_guard<std::mutex> lock(mu_);
    app_->setSetpoint(celsius);
    return "";
}

std::string LiveSimulation::setLoad(double pct) {
    if (!std::isfinite(pct) || pct < 0.0 || pct > 100.0) return "load must be between 0 and 100 %";
    std::lock_guard<std::mutex> lock(mu_);
    loadPct_ = pct;
    return "";
}

std::string LiveSimulation::setSpeed(double factor) {
    if (!std::isfinite(factor) || factor < 0.5 || factor > 50.0) return "speed must be between 0.5 and 50";
    std::lock_guard<std::mutex> lock(mu_);
    speed_ = factor;
    return "";
}

std::string LiveSimulation::inject(const std::string& faultName) {
    const auto fault = injectedFaultFromString(faultName);
    if (!fault || *fault == InjectedFault::None) return "unknown fault '" + faultName + "'";
    std::lock_guard<std::mutex> lock(mu_);
    models_[active_]->injectFault(*fault);
    if (std::find(injectedNames_.begin(), injectedNames_.end(), faultName) == injectedNames_.end()) {
        injectedNames_.push_back(faultName);
    }
    return "";
}

void LiveSimulation::resetAlarms() {
    std::lock_guard<std::mutex> lock(mu_);
    const ChillerModel& model = *models_[active_];
    for (const DetectedFault& f : app_->activeFaults()) {
        logger_->logFaultEvent(model.simTimeS(), model.config().modelId, f, "CLEARED");
    }
    app_->resetAlarms();
    injectedNames_.clear();
}

void LiveSimulation::restart() {
    std::lock_guard<std::mutex> lock(mu_);
    startRunLocked();
}

}  // namespace chiller
