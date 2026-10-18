#pragma once
// Batch runners used by the command-line tool and the tests:
//   runScenario   - one model through the scenario, optional fault injection
//   runFaultTrial - inject one fault and measure detection and latency

#include <cstdint>
#include <optional>
#include <vector>

#include "chiller/blackbox_logger.hpp"
#include "chiller/chiller_model.hpp"
#include "chiller/metrics.hpp"
#include "chiller/scenario.hpp"

namespace chiller {

struct Injection {
    double atS = 0.0;
    InjectedFault fault = InjectedFault::None;
};

struct TimedFault {
    double timeS = 0.0;
    DetectedFault fault;
};

struct RunResult {
    std::vector<BlackBoxRecord> records;
    std::vector<TimedFault> faults;  // first detection of each fault code
    RunMetrics metrics;
    std::int64_t runId = -1;
};

struct RunOptions {
    BlackBoxLogger* logger = nullptr;   // optional: store records in SQLite
    std::optional<Injection> injection;
    std::optional<double> stopAfterS;   // stop early (absolute simulation time)
    std::string notes;
};

RunResult runScenario(ChillerModel& model, const Scenario& scenario, const RunOptions& options = {});

struct FaultTrial {
    std::string modelId;
    std::string modelType;
    InjectedFault injected = InjectedFault::None;
    FaultCode expected = FaultCode::None;
    bool detected = false;               // expected code raised inside the window
    FaultCode firstCode = FaultCode::None;  // first code raised after the injection
    std::string nativeCode;              // native code of the expected fault
    std::optional<double> latencyS;      // detection time - injection time
    bool tripped = false;
};

FaultTrial runFaultTrial(const ChillerConfig& config, const Scenario& scenario, InjectedFault fault,
                         BlackBoxLogger* logger = nullptr);

// Runs the scenario with no injected fault and returns how many faults were raised.
int countFalseAlarms(const ChillerConfig& config, const Scenario& scenario, BlackBoxLogger* logger = nullptr);

}  // namespace chiller
