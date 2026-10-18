#include "chiller/metrics.hpp"

#include <algorithm>
#include <cmath>

namespace chiller {

RunMetrics computeMetrics(const std::vector<BlackBoxRecord>& records, const Scenario& sc, int compressorStarts) {
    RunMetrics m;
    m.compressorStarts = compressorStarts;
    m.samples = static_cast<int>(records.size());
    if (records.empty()) return m;

    const double sp = sc.setpointC;
    const double band = sc.settleBandC;
    const double stepT = sc.loadStepTimeS;
    const double dt = sc.dtS;
    const double endT = records.back().timeS;

    // Startup phase: [0, load step)
    double minSupply = sp;
    std::optional<double> lastOutsideBeforeStep;
    bool anyBeforeStep = false;
    for (const auto& r : records) {
        if (r.timeS >= stepT) break;
        anyBeforeStep = true;
        minSupply = std::min(minSupply, r.chwSupplyC);
        if (std::abs(r.chwSupplyC - sp) > band) lastOutsideBeforeStep = r.timeS;
    }
    m.undershootC = std::max(0.0, sp - minSupply);
    if (anyBeforeStep) {
        if (!lastOutsideBeforeStep) {
            m.settlingTimeS = 0.0;
        } else if (*lastOutsideBeforeStep + dt < stepT) {
            m.settlingTimeS = *lastOutsideBeforeStep + dt;
        }
    }

    // Load-step phase: [load step, end]
    std::optional<double> lastOutsideAfterStep;
    bool anyAfterStep = false;
    for (const auto& r : records) {
        if (r.timeS < stepT) continue;
        anyAfterStep = true;
        m.peakDeviationAfterStepC = std::max(m.peakDeviationAfterStepC, r.chwSupplyC - sp);
        if (std::abs(r.chwSupplyC - sp) > band) lastOutsideAfterStep = r.timeS;
    }
    if (anyAfterStep) {
        if (!lastOutsideAfterStep) {
            m.recoveryTimeS = 0.0;
        } else if (*lastOutsideAfterStep < endT) {
            m.recoveryTimeS = *lastOutsideAfterStep + dt - stepT;
        }
    }

    // Steady-state error over the final window.
    double sum = 0.0;
    int n = 0;
    for (const auto& r : records) {
        if (r.timeS > endT - sc.steadyWindowS) {
            sum += std::abs(r.chwSupplyC - sp);
            m.maxSteadyErrorC = std::max(m.maxSteadyErrorC, std::abs(r.chwSupplyC - sp));
            ++n;
        }
    }
    m.steadyStateErrorC = n ? sum / n : 0.0;

    // RMS error after the startup transient.
    const double from = m.settlingTimeS.value_or(stepT);
    double sq = 0.0;
    int k = 0;
    for (const auto& r : records) {
        if (r.timeS >= from) {
            sq += (r.chwSupplyC - sp) * (r.chwSupplyC - sp);
            ++k;
        }
    }
    m.rmsErrorAfterSettleC = k ? std::sqrt(sq / k) : 0.0;

    for (const auto& r : records) m.energyKwh += r.powerKw * dt / 3600.0;
    return m;
}

}  // namespace chiller
