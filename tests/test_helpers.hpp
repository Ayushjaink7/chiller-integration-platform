#pragma once
// Shared helpers for the unit tests. CHILLER_CONFIG_DIR is set by CMake.

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "chiller/config.hpp"
#include "chiller/scenario.hpp"

namespace testutil {

// Config folder: $CHILLER_CONFIG_DIR if set, else the source tree's config/
// (baked in by CMake), else ./config so a copied test binary still works when
// started from the project folder.
inline std::string configDir() {
    if (const char* env = std::getenv("CHILLER_CONFIG_DIR")) return env;
    if (std::filesystem::exists(CHILLER_CONFIG_DIR)) return CHILLER_CONFIG_DIR;
    return "config";
}
inline std::string modelsDir() { return configDir() + "/models"; }

inline chiller::ChillerConfig modelConfig(const std::string& type) {
    for (const auto& c : chiller::loadAllChillerConfigs(modelsDir())) {
        if (c.type == type) return c;
    }
    throw std::runtime_error("no shipped config for type " + type);
}

inline chiller::Scenario defaultScenario() { return chiller::loadScenario(configDir() + "/scenario.json"); }

inline const std::vector<std::string>& modelTypes() {
    static const std::vector<std::string> types = {"screw", "centrifugal", "scroll"};
    return types;
}

}  // namespace testutil
