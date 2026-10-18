#pragma once
// BlackBoxLogger: stores standardized records from every model in SQLite.
//
// Tables
//   runs          one row per simulation run (model, scenario, start time)
//   blackbox      one row per control tick: the same columns for every model,
//                 plus extras_json for model-specific sensors
//   fault_events  RAISED / CLEARED events with standard and native fault codes
//
// Writes are batched in transactions for speed. All methods are thread-safe.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "chiller/types.hpp"

namespace chiller {

struct FaultEvent {
    std::int64_t id = 0;
    std::int64_t runId = 0;
    double timeS = 0.0;
    std::string modelId;
    std::string faultCode;
    std::string nativeCode;
    std::string description;
    std::string event;      // RAISED / CLEARED
    std::string wallClock;  // UTC time the event was written
};

struct RunInfo {
    std::int64_t runId = 0;
    std::string startedAt;
    std::string modelId;
    std::string modelType;
    std::string scenario;
    std::string notes;
    std::int64_t records = 0;
};

class BlackBoxLogger {
public:
    // path can be a file or ":memory:". Parent directories are created.
    explicit BlackBoxLogger(const std::string& path, int batchSize = 200);
    ~BlackBoxLogger();
    BlackBoxLogger(const BlackBoxLogger&) = delete;
    BlackBoxLogger& operator=(const BlackBoxLogger&) = delete;

    std::int64_t beginRun(const std::string& modelId, const std::string& modelType, const std::string& scenario,
                          const std::string& notes = "");
    std::int64_t currentRunId() const;

    void write(const BlackBoxRecord& record);
    void logFaultEvent(double timeS, const std::string& modelId, const DetectedFault& fault,
                       const std::string& event);
    void flush();

    std::vector<BlackBoxRecord> readRun(std::int64_t runId) const;
    std::int64_t countRecords(std::int64_t runId) const;
    std::vector<FaultEvent> recentFaultEvents(int limit) const;
    std::vector<RunInfo> recentRuns(int limit) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace chiller
