#include <cmath>
#include <sstream>

#include "chiller/models.hpp"

namespace chiller {

ScrollChiller::ScrollChiller(ChillerConfig cfg, std::uint32_t seed)
    : SimulatedChiller(std::move(cfg), seed),
      stages_(static_cast<int>(std::lround(config().param("stages")))),
      minSwitchS_(config().param("min_switch_interval_s")),
      upMarginPct_(config().param("stage_up_margin_pct")),
      downMarginPct_(config().param("stage_down_margin_pct")),
      dischargeBaseC_(config().param("discharge_temp_base_c")),
      dischargePerPct_(config().param("discharge_temp_per_capacity_pct")),
      dischargeFailedC_(config().param("discharge_temp_failed_c")),
      dischargeMaxC_(config().param("discharge_temp_max_c")),
      dischargeTauS_(config().param("discharge_time_constant_s")) {
    if (stages_ < 1) throw ConfigError(config().sourceFile + ": model_params.stages must be >= 1");
    resetModelSpecific();
}

double ScrollChiller::translateRequest(double requestPct) {
    // The PID asks for any percentage, but this model can only switch whole
    // compressors on or off. Add a stage when the request is clearly above the
    // current capacity, drop one when it is clearly below, and never switch
    // more often than min_switch_interval_s (protects the compressors).
    const double stepPct = 100.0 / stages_;
    const double current = activeStages_ * stepPct;
    const bool canSwitch = simTimeS() - lastSwitchS_ >= minSwitchS_;
    if (canSwitch) {
        if (activeStages_ < stages_ && requestPct > current + upMarginPct_) {
            ++activeStages_;
            countStart();
            lastSwitchS_ = simTimeS();
        } else if (activeStages_ > 0 && requestPct < current - downMarginPct_) {
            --activeStages_;
            lastSwitchS_ = simTimeS();
        }
    }
    return activeStages_ * stepPct;
}

double ScrollChiller::normalDischargeTemp() const { return dischargeBaseC_ + dischargePerPct_ * actualPct(); }

void ScrollChiller::stepModelSpecific(double dtS) {
    const double target =
        isInjected(InjectedFault::CompressorProtection) ? dischargeFailedC_ : normalDischargeTemp();
    dischargeTempC_ = approach(dischargeTempC_, target, dtS, dischargeTauS_);
}

void ScrollChiller::addModelSensors(SensorData& s) {
    s.extra["active_stages"] = activeStages_;
    s.extra["discharge_temp_c"] = dischargeTempC_ + gaussianNoise(config().noise.temperatureC * 10.0);
}

std::optional<DetectedFault> ScrollChiller::checkModelFaults(const SensorData& s) const {
    auto it = s.extra.find("discharge_temp_c");
    if (it != s.extra.end() && it->second > dischargeMaxC_) {
        std::ostringstream os;
        os.setf(std::ios::fixed);
        os.precision(1);
        os << "discharge temperature " << it->second << " C > " << dischargeMaxC_ << " C";
        return makeFault(FaultCode::CompressorProtection, os.str());
    }
    return std::nullopt;
}

void ScrollChiller::onTrip() { activeStages_ = 0; }

void ScrollChiller::resetModelSpecific() {
    activeStages_ = 0;
    lastSwitchS_ = -1e9;
    dischargeTempC_ = normalDischargeTemp();
}

void ScrollChiller::repairModelSpecific() { dischargeTempC_ = normalDischargeTemp(); }

}  // namespace chiller
