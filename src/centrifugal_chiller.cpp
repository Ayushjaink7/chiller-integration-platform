#include <sstream>

#include "chiller/models.hpp"

namespace chiller {

CentrifugalChiller::CentrifugalChiller(ChillerConfig cfg, std::uint32_t seed)
    : SimulatedChiller(std::move(cfg), seed),
      marginBasePct_(config().param("surge_margin_base_pct")),
      marginPerPct_(config().param("surge_margin_per_capacity_pct")),
      marginFailedPct_(config().param("surge_margin_failed_pct")),
      marginMinPct_(config().param("surge_margin_min_pct")),
      marginTauS_(config().param("surge_time_constant_s")) {
    resetModelSpecific();
}

double CentrifugalChiller::translateRequest(double requestPct) {
    // Guide vanes: smooth control, but never below the surge-safe minimum.
    return applyMinimumCapacity(requestPct, config().capacity.minCapacityPct);
}

double CentrifugalChiller::normalSurgeMargin() const {
    // More load means more gas flow through the impeller and more margin from surge.
    return marginBasePct_ + marginPerPct_ * (actualPct() - config().capacity.minCapacityPct);
}

void CentrifugalChiller::stepModelSpecific(double dtS) {
    const double target = isInjected(InjectedFault::CompressorProtection) ? marginFailedPct_ : normalSurgeMargin();
    surgeMarginPct_ = approach(surgeMarginPct_, target, dtS, marginTauS_);
}

void CentrifugalChiller::addModelSensors(SensorData& s) {
    s.extra["guide_vane_position_pct"] = actualPct();
    s.extra["surge_margin_pct"] = surgeMarginPct_;
}

std::optional<DetectedFault> CentrifugalChiller::checkModelFaults(const SensorData& s) const {
    auto it = s.extra.find("surge_margin_pct");
    if (it != s.extra.end() && it->second < marginMinPct_) {
        std::ostringstream os;
        os.setf(std::ios::fixed);
        os.precision(1);
        os << "surge margin " << it->second << " % < " << marginMinPct_ << " %";
        return makeFault(FaultCode::CompressorProtection, os.str());
    }
    return std::nullopt;
}

void CentrifugalChiller::resetModelSpecific() { surgeMarginPct_ = normalSurgeMargin(); }
void CentrifugalChiller::repairModelSpecific() { surgeMarginPct_ = normalSurgeMargin(); }

}  // namespace chiller
