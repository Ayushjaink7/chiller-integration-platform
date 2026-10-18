#pragma once
// A test scenario: setpoint, PID gains, building-load profile and run length.
// Loaded from config/scenario.json. The same scenario is used for every model.

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace chiller {

struct PidGains {
    double kp = 0.0;
    double ki = 0.0;
    double kd = 0.0;
    bool actuatorFeedback = true;  // back-calculation anti-windup, see PidController
    double trackingTimeS = 60.0;   // Tt for back-calculation
};

struct LoadPoint {
    double timeS = 0.0;
    double loadPct = 0.0;  // building cooling load as % of the chiller's rated capacity
};

struct Scenario {
    std::string name = "default";
    std::string description;
    double setpointC = 7.0;
    double dtS = 1.0;
    double durationS = 900.0;
    std::uint32_t seed = 42;
    PidGains pid{25.0, 0.15, 0.0, true, 60.0};
    std::vector<LoadPoint> loadProfile{{0.0, 50.0}, {400.0, 80.0}};
    double settleBandC = 0.25;
    double loadStepTimeS = 400.0;
    double steadyWindowS = 60.0;
    double injectAtS = 500.0;
    double detectionWindowS = 400.0;

    // Step profile: the load is the value of the last point whose time <= t.
    double loadPctAt(double timeS) const;
};

Scenario parseScenario(const nlohmann::json& j, const std::string& sourceName);
Scenario loadScenario(const std::string& path);

}  // namespace chiller
