#pragma once
// Control-quality metrics computed from black-box records.

#include <optional>
#include <vector>

#include "chiller/scenario.hpp"
#include "chiller/types.hpp"

namespace chiller {

struct RunMetrics {
    double undershootC = 0.0;               // startup: how far supply water went below setpoint
    std::optional<double> settlingTimeS;    // startup: time until it stays inside the band
    double peakDeviationAfterStepC = 0.0;   // load step: worst warm-up above setpoint
    std::optional<double> recoveryTimeS;    // load step: time until back inside the band for good
    double steadyStateErrorC = 0.0;         // mean |error| over the last steady window
    double maxSteadyErrorC = 0.0;           // worst |error| over the last steady window (ripple)
    double rmsErrorAfterSettleC = 0.0;      // RMS error from settling to the end
    int compressorStarts = 0;
    double energyKwh = 0.0;                 // compressor electrical energy
    int samples = 0;
};

RunMetrics computeMetrics(const std::vector<BlackBoxRecord>& records, const Scenario& scenario,
                          int compressorStarts);

}  // namespace chiller
