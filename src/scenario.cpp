#include "chiller/scenario.hpp"

#include <algorithm>
#include <fstream>

#include "chiller/config.hpp"

namespace chiller {

double Scenario::loadPctAt(double timeS) const {
    double load = loadProfile.empty() ? 0.0 : loadProfile.front().loadPct;
    for (const LoadPoint& p : loadProfile) {
        if (p.timeS <= timeS) load = p.loadPct;
    }
    return load;
}

Scenario parseScenario(const nlohmann::json& j, const std::string& where) {
    Scenario s;
    s.name = j.value("name", s.name);
    s.description = j.value("description", "");
    s.setpointC = j.value("setpoint_c", s.setpointC);
    s.dtS = j.value("dt_s", s.dtS);
    s.durationS = j.value("duration_s", s.durationS);
    s.seed = j.value("seed", s.seed);
    if (j.contains("pid")) {
        const auto& p = j.at("pid");
        s.pid = {p.value("kp", 0.0), p.value("ki", 0.0), p.value("kd", 0.0), p.value("actuator_feedback", true),
                 p.value("tracking_time_s", 60.0)};
    }
    if (j.contains("load_profile")) {
        s.loadProfile.clear();
        for (const auto& point : j.at("load_profile")) {
            s.loadProfile.push_back({point.at("time_s").get<double>(), point.at("load_pct").get<double>()});
        }
        std::sort(s.loadProfile.begin(), s.loadProfile.end(),
                  [](const LoadPoint& a, const LoadPoint& b) { return a.timeS < b.timeS; });
    }
    if (j.contains("metrics")) {
        const auto& m = j.at("metrics");
        s.settleBandC = m.value("settle_band_c", s.settleBandC);
        s.loadStepTimeS = m.value("load_step_time_s", s.loadStepTimeS);
        s.steadyWindowS = m.value("steady_window_s", s.steadyWindowS);
    }
    if (j.contains("fault_campaign")) {
        const auto& f = j.at("fault_campaign");
        s.injectAtS = f.value("inject_at_s", s.injectAtS);
        s.detectionWindowS = f.value("detection_window_s", s.detectionWindowS);
    }
    if (s.dtS <= 0 || s.durationS <= 0) throw ConfigError(where + ": dt_s and duration_s must be > 0");
    if (s.loadProfile.empty()) throw ConfigError(where + ": load_profile must not be empty");
    return s;
}

Scenario loadScenario(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw ConfigError("cannot open scenario file: " + path);
    nlohmann::json j;
    try {
        in >> j;
    } catch (const nlohmann::json::parse_error& e) {
        throw ConfigError(path + ": invalid JSON: " + e.what());
    }
    return parseScenario(j, path);
}

}  // namespace chiller
