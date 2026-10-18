#include <sstream>

#include "chiller/models.hpp"

namespace chiller {

ScrewChiller::ScrewChiller(ChillerConfig cfg, std::uint32_t seed)
    : SimulatedChiller(std::move(cfg), seed),
      oilNominalKpa_(config().param("oil_pressure_nominal_kpa")),
      oilFailedKpa_(config().param("oil_pressure_failed_kpa")),
      oilMinKpa_(config().param("oil_pressure_min_kpa")),
      oilTauS_(config().param("oil_time_constant_s")) {
    resetModelSpecific();
}

double ScrewChiller::translateRequest(double requestPct) {
    // Slide valve: any position between the minimum and 100 %.
    return applyMinimumCapacity(requestPct, config().capacity.minCapacityPct);
}

void ScrewChiller::stepModelSpecific(double dtS) {
    const double target = isInjected(InjectedFault::CompressorProtection) ? oilFailedKpa_ : oilNominalKpa_;
    oilPressureKpa_ = approach(oilPressureKpa_, target, dtS, oilTauS_);
}

void ScrewChiller::addModelSensors(SensorData& s) {
    s.extra["slide_valve_position_pct"] = actualPct();
    s.extra["oil_pressure_kpa"] = oilPressureKpa_ + gaussianNoise(config().noise.pressureKpa);
}

std::optional<DetectedFault> ScrewChiller::checkModelFaults(const SensorData& s) const {
    auto it = s.extra.find("oil_pressure_kpa");
    if (it != s.extra.end() && it->second < oilMinKpa_) {
        std::ostringstream os;
        os << "low oil pressure " << static_cast<int>(it->second) << " kPa < " << static_cast<int>(oilMinKpa_);
        return makeFault(FaultCode::CompressorProtection, os.str());
    }
    return std::nullopt;
}

void ScrewChiller::resetModelSpecific() { oilPressureKpa_ = oilNominalKpa_; }
void ScrewChiller::repairModelSpecific() { oilPressureKpa_ = oilNominalKpa_; }

}  // namespace chiller
