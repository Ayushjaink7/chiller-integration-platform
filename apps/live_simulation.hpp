#pragma once
// LiveSimulation: runs the control app in real time (scaled by a speed factor)
// on a background thread, for the REST API and the dashboard.
// All public methods are thread-safe.

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "chiller/blackbox_logger.hpp"
#include "chiller/control_app.hpp"
#include "chiller/scenario.hpp"

namespace chiller {

struct LiveOptions {
    std::string configDir = "config";
    std::string scenarioPath;  // default: <configDir>/scenario.json
    std::string dbPath = "data/live.db";
    double speed = 5.0;               // simulated seconds per real second
    std::size_t historyLimit = 3600;  // ticks kept in memory for charts
};

class LiveSimulation {
public:
    explicit LiveSimulation(LiveOptions options);
    ~LiveSimulation();
    LiveSimulation(const LiveSimulation&) = delete;
    LiveSimulation& operator=(const LiveSimulation&) = delete;

    void start();
    void stop();

    nlohmann::json models() const;
    nlohmann::json state() const;
    nlohmann::json history(double seconds) const;
    nlohmann::json faults(int limit) const;
    nlohmann::json runs(int limit) const;

    // Commands. They return an error message, or "" on success.
    std::string selectModel(const std::string& modelId);
    std::string setSetpoint(double celsius);
    std::string setLoad(double pct);
    std::string setSpeed(double factor);
    std::string inject(const std::string& faultName);
    void resetAlarms();
    void restart();

private:
    void loop();
    void tickLocked();
    void startRunLocked();
    nlohmann::json stateLocked() const;

    LiveOptions opt_;
    Scenario scenario_;
    std::vector<ChillerConfig> configs_;
    std::vector<std::unique_ptr<ChillerModel>> models_;
    std::size_t active_ = 0;
    std::unique_ptr<ControlApp> app_;
    std::unique_ptr<BlackBoxLogger> logger_;

    double loadPct_ = 50.0;
    double speed_ = 5.0;
    std::deque<BlackBoxRecord> history_;
    std::vector<std::string> injectedNames_;

    mutable std::mutex mu_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace chiller
