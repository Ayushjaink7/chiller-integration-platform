// chiller_sim: command-line runner for the Multi-Model Chiller Integration Platform.
//
//   chiller_sim list
//   chiller_sim run <model-id|type|file.json> [--csv out.csv]
//   chiller_sim benchmark            all models, same scenario, same PID
//   chiller_sim faults               fault-injection campaign on all models
//
// Common options:
//   --config-dir <dir>   default: config        (models in <dir>/models)
//   --scenario <file>    default: <config-dir>/scenario.json
//   --db <file>          default: data/blackbox.db
//   --out <dir>          default: results       (benchmark / faults reports)

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "chiller/blackbox_logger.hpp"
#include "chiller/config.hpp"
#include "chiller/model_factory.hpp"
#include "chiller/runner.hpp"
#include "chiller/scenario.hpp"

using namespace chiller;
namespace fs = std::filesystem;

namespace {

struct Options {
    std::string command;
    std::vector<std::string> positional;
    std::string configDir = "config";
    std::string scenarioPath;
    std::string dbPath = "data/blackbox.db";
    std::string outDir = "results";
    std::string csvPath;
};

void printUsage() {
    std::cout << "Multi-Model Chiller Integration Platform\n\n"
                 "Usage:\n"
                 "  chiller_sim list\n"
                 "  chiller_sim run <model-id|type|file.json> [--csv out.csv]\n"
                 "  chiller_sim benchmark\n"
                 "  chiller_sim faults\n\n"
                 "Options:\n"
                 "  --config-dir <dir>   model and scenario configs (default: config)\n"
                 "  --scenario <file>    scenario file (default: <config-dir>/scenario.json)\n"
                 "  --db <file>          SQLite black-box database (default: data/blackbox.db)\n"
                 "  --out <dir>          report folder (default: results)\n";
}

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("missing value after " + name);
            return argv[++i];
        };
        if (a == "--config-dir") o.configDir = next(a);
        else if (a == "--scenario") o.scenarioPath = next(a);
        else if (a == "--db") o.dbPath = next(a);
        else if (a == "--out") o.outDir = next(a);
        else if (a == "--csv") o.csvPath = next(a);
        else if (a == "-h" || a == "--help") o.command = "help";
        else if (!a.empty() && a[0] == '-') throw std::runtime_error("unknown option " + a);
        else if (o.command.empty()) o.command = a;
        else o.positional.push_back(a);
    }
    if (o.scenarioPath.empty()) o.scenarioPath = (fs::path(o.configDir) / "scenario.json").string();
    return o;
}

std::string fixed(double v, int p) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(p) << v;
    return os.str();
}

std::string optSeconds(const std::optional<double>& v) { return v ? fixed(*v, 0) + " s" : "not reached"; }

// Simple text table that also renders as Markdown.
class Table {
public:
    explicit Table(std::vector<std::string> header) : header_(std::move(header)) {}
    void add(std::vector<std::string> row) { rows_.push_back(std::move(row)); }

    std::string markdown() const {
        std::vector<std::size_t> w(header_.size(), 3);
        for (std::size_t c = 0; c < header_.size(); ++c) {
            w[c] = std::max(w[c], header_[c].size());
            for (const auto& r : rows_) w[c] = std::max(w[c], r[c].size());
        }
        auto line = [&](const std::vector<std::string>& cells) {
            std::string s = "|";
            for (std::size_t c = 0; c < cells.size(); ++c) {
                s += " " + cells[c] + std::string(w[c] - cells[c].size(), ' ') + " |";
            }
            return s + "\n";
        };
        std::string out = line(header_);
        std::string sep = "|";
        for (std::size_t c = 0; c < header_.size(); ++c) sep += std::string(w[c] + 2, '-') + "|";
        out += sep + "\n";
        for (const auto& r : rows_) out += line(r);
        return out;
    }

private:
    std::vector<std::string> header_;
    std::vector<std::vector<std::string>> rows_;
};

ChillerConfig resolveModel(const Options& o, const std::string& key) {
    if (fs::path(key).extension() == ".json" && fs::exists(key)) return loadChillerConfig(key);
    for (const auto& cfg : loadAllChillerConfigs((fs::path(o.configDir) / "models").string())) {
        if (cfg.modelId == key || cfg.type == key) return cfg;
    }
    throw std::runtime_error("no model with id or type '" + key + "' in " + o.configDir + "/models");
}

void writeFile(const fs::path& path, const std::string& content) {
    if (path.has_parent_path()) fs::create_directories(path.parent_path());
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out << content;
}

int cmdList(const Options& o) {
    Table t({"Model ID", "Type", "Rated", "Capacity control", "Min %", "Model-specific sensors"});
    for (const auto& c : loadAllChillerConfigs((fs::path(o.configDir) / "models").string())) {
        std::string extras;
        for (std::size_t i = 5; i < c.sensors.size(); ++i) extras += (extras.empty() ? "" : ", ") + c.sensors[i];
        t.add({c.modelId, c.type, fixed(c.ratedCapacityKw, 0) + " kW", c.capacity.method,
               fixed(c.capacity.minCapacityPct, 0), extras});
    }
    std::cout << t.markdown();
    std::cout << "\nRegistered model types:";
    for (const auto& name : registeredChillerTypes()) std::cout << " " << name;
    std::cout << "\n";
    return 0;
}

void printMetrics(const ChillerConfig& cfg, const RunMetrics& m) {
    std::cout << cfg.modelId << " (" << cfg.type << ")\n"
              << "  startup undershoot        : " << fixed(m.undershootC, 2) << " C\n"
              << "  startup settling time     : " << optSeconds(m.settlingTimeS) << "\n"
              << "  peak rise after load step : " << fixed(m.peakDeviationAfterStepC, 2) << " C\n"
              << "  recovery after load step  : " << optSeconds(m.recoveryTimeS) << "\n"
              << "  steady-state error (mean) : " << fixed(m.steadyStateErrorC, 3) << " C\n"
              << "  steady-state error (max)  : " << fixed(m.maxSteadyErrorC, 2) << " C\n"
              << "  RMS error after settling  : " << fixed(m.rmsErrorAfterSettleC, 3) << " C\n"
              << "  compressor starts         : " << m.compressorStarts << "\n"
              << "  compressor energy         : " << fixed(m.energyKwh, 1) << " kWh\n";
}

int cmdRun(const Options& o) {
    if (o.positional.empty()) throw std::runtime_error("run needs a model id, type or file");
    const ChillerConfig cfg = resolveModel(o, o.positional[0]);
    const Scenario sc = loadScenario(o.scenarioPath);
    BlackBoxLogger logger(o.dbPath);
    auto model = makeChiller(cfg, sc.seed);

    RunOptions opt;
    opt.logger = &logger;
    const RunResult r = runScenario(*model, sc, opt);
    printMetrics(cfg, r.metrics);
    std::cout << "  black-box records         : " << logger.countRecords(r.runId) << " (run " << r.runId << " in "
              << o.dbPath << ")\n";

    if (!o.csvPath.empty()) {
        std::ostringstream csv;
        csv << "t_s,setpoint_c,load_pct,chw_supply_c,chw_return_c,capacity_request_pct,capacity_command_pct,"
               "capacity_actual_pct,cond_pressure_kpa,power_kw,state,fault_code\n";
        for (const auto& rec : r.records) {
            csv << rec.timeS << ',' << rec.setpointC << ',' << rec.loadPct << ',' << fixed(rec.chwSupplyC, 3) << ','
                << fixed(rec.chwReturnC, 3) << ',' << fixed(rec.capacityRequestPct, 2) << ','
                << fixed(rec.capacityCommandPct, 2) << ',' << fixed(rec.capacityActualPct, 2) << ','
                << fixed(rec.condPressureKpa, 1) << ',' << fixed(rec.powerKw, 2) << ',' << rec.state << ','
                << rec.faultCode << '\n';
        }
        writeFile(o.csvPath, csv.str());
        std::cout << "  CSV written to " << o.csvPath << "\n";
    }
    return 0;
}

int cmdBenchmark(const Options& o) {
    const Scenario sc = loadScenario(o.scenarioPath);
    const auto configs = loadAllChillerConfigs((fs::path(o.configDir) / "models").string());
    BlackBoxLogger logger(o.dbPath);

    Table t({"Model", "Type", "Capacity control", "Undershoot", "Settling", "Peak rise after step",
             "Recovery after step", "Mean error (last " + fixed(sc.steadyWindowS, 0) + " s)", "Max error (last " +
                 fixed(sc.steadyWindowS, 0) + " s)", "Compressor starts", "Records in SQLite"});
    std::ostringstream csv;
    csv << "model_id,type,undershoot_c,settling_s,peak_rise_c,recovery_s,steady_state_error_c,max_steady_error_c,"
           "rms_error_c,"
           "compressor_starts,energy_kwh,records\n";

    const auto start = std::chrono::steady_clock::now();
    std::int64_t totalRecords = 0;
    for (const auto& cfg : configs) {
        auto model = makeChiller(cfg, sc.seed);  // the same control code runs on every model
        RunOptions opt;
        opt.logger = &logger;
        opt.notes = "benchmark";
        const RunResult r = runScenario(*model, sc, opt);
        const RunMetrics& m = r.metrics;
        const std::int64_t n = logger.countRecords(r.runId);
        totalRecords += n;
        printMetrics(cfg, m);
        t.add({cfg.modelId, cfg.type, cfg.capacity.method, fixed(m.undershootC, 2) + " C", optSeconds(m.settlingTimeS),
               fixed(m.peakDeviationAfterStepC, 2) + " C", optSeconds(m.recoveryTimeS),
               fixed(m.steadyStateErrorC, 3) + " C", "+/-" + fixed(m.maxSteadyErrorC, 2) + " C",
               std::to_string(m.compressorStarts), std::to_string(n)});
        csv << cfg.modelId << ',' << cfg.type << ',' << fixed(m.undershootC, 3) << ','
            << (m.settlingTimeS ? fixed(*m.settlingTimeS, 0) : "") << ',' << fixed(m.peakDeviationAfterStepC, 3)
            << ',' << (m.recoveryTimeS ? fixed(*m.recoveryTimeS, 0) : "") << ',' << fixed(m.steadyStateErrorC, 4)
            << ',' << fixed(m.maxSteadyErrorC, 3) << ',' << fixed(m.rmsErrorAfterSettleC, 4) << ',' << m.compressorStarts << ',' << fixed(m.energyKwh, 2)
            << ',' << n << '\n';
    }
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    std::ostringstream md;
    md << "# Benchmark: one PID control app on " << configs.size() << " chiller models\n\n"
       << "Scenario: " << sc.description << "\n\n"
       << "Setpoint " << fixed(sc.setpointC, 1) << " C, PID Kp=" << sc.pid.kp << " Ki=" << sc.pid.ki
       << " Kd=" << sc.pid.kd << " (identical for every model), settle band +/-" << fixed(sc.settleBandC, 2)
       << " C, time step " << sc.dtS << " s, " << sc.durationS << " s per run.\n\n"
       << t.markdown() << "\n"
       << totalRecords << " standardized black-box records written to SQLite in " << fixed(ms, 0) << " ms.\n";
    writeFile(fs::path(o.outDir) / "benchmark.md", md.str());
    writeFile(fs::path(o.outDir) / "benchmark.csv", csv.str());
    std::cout << "\n" << t.markdown() << "\nReports written to " << o.outDir << "/benchmark.md and benchmark.csv\n";
    return 0;
}

int cmdFaults(const Options& o) {
    const Scenario sc = loadScenario(o.scenarioPath);
    const auto configs = loadAllChillerConfigs((fs::path(o.configDir) / "models").string());
    BlackBoxLogger logger(o.dbPath);

    Table t({"Model", "Injected fault", "Expected", "Native code", "Detected", "Latency", "Tripped"});
    std::ostringstream csv;
    csv << "model_id,type,injected_fault,expected_code,native_code,detected,first_code,latency_s,tripped\n";
    int trials = 0, detected = 0, falseAlarms = 0;
    double latencySum = 0.0, latencyMax = 0.0;

    for (const auto& cfg : configs) {
        falseAlarms += countFalseAlarms(cfg, sc, &logger);
        for (InjectedFault f : allInjectableFaults()) {
            const FaultTrial tr = runFaultTrial(cfg, sc, f, &logger);
            ++trials;
            if (tr.detected) {
                ++detected;
                latencySum += *tr.latencyS;
                latencyMax = std::max(latencyMax, *tr.latencyS);
            }
            t.add({cfg.modelId, toString(f), toString(tr.expected), tr.nativeCode, tr.detected ? "yes" : "NO",
                   tr.latencyS ? fixed(*tr.latencyS, 0) + " s" : "-", tr.tripped ? "yes" : "no"});
            csv << cfg.modelId << ',' << cfg.type << ',' << toString(f) << ',' << toString(tr.expected) << ','
                << tr.nativeCode << ',' << (tr.detected ? 1 : 0) << ',' << toString(tr.firstCode) << ','
                << (tr.latencyS ? fixed(*tr.latencyS, 0) : "") << ',' << (tr.tripped ? 1 : 0) << '\n';
        }
    }

    std::ostringstream summary;
    summary << "Detected " << detected << " / " << trials << " injected faults with the correct standard code";
    if (detected > 0) {
        summary << ", mean latency " << fixed(latencySum / detected, 1) << " s, max " << fixed(latencyMax, 0)
                << " s";
    }
    summary << ". False alarms in " << configs.size() << " fault-free runs: " << falseAlarms << ".";

    std::ostringstream md;
    md << "# Fault-injection campaign\n\n"
       << "Each fault is injected at t = " << fixed(sc.injectAtS, 0) << " s into the normal scenario and watched for "
       << fixed(sc.detectionWindowS, 0) << " s. Native model codes are mapped to standard codes F01-F06.\n\n"
       << t.markdown() << "\n" << summary.str() << "\n";
    writeFile(fs::path(o.outDir) / "fault_campaign.md", md.str());
    writeFile(fs::path(o.outDir) / "fault_campaign.csv", csv.str());
    std::cout << t.markdown() << "\n" << summary.str() << "\nReports written to " << o.outDir
              << "/fault_campaign.md and fault_campaign.csv\n";
    return detected == trials && falseAlarms == 0 ? 0 : 2;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options o = parseArgs(argc, argv);
        if (o.command.empty() || o.command == "help") {
            printUsage();
            return o.command.empty() ? 1 : 0;
        }
        if (o.command == "list") return cmdList(o);
        if (o.command == "run") return cmdRun(o);
        if (o.command == "benchmark") return cmdBenchmark(o);
        if (o.command == "faults") return cmdFaults(o);
        std::cerr << "Unknown command '" << o.command << "'\n\n";
        printUsage();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
