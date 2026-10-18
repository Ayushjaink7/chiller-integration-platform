#pragma once
// Per-model configuration, loaded from config/models/<model>.json.
//
// Everything that differs between chiller models lives here, not in code:
// capacity, limits, time constants, sensor list and native fault codes.

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "chiller/types.hpp"

namespace chiller {

class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct CapacityControl {
    std::string method;           // slide_valve / inlet_guide_vanes / compressor_staging
    double minCapacityPct = 0.0;  // lowest continuous capacity the model allows
    double responseTimeS = 1.0;   // first-order lag of the compressor
};

struct WaterLoop {
    double thermalMassKjPerC = 1.0;
    double nominalFlowKgS = 1.0;
    double initialSupplyTempC = 12.0;
    double flowTimeConstantS = 5.0;
    // Building air temperature: with the chiller off, the water warms towards it
    // but never above it. Load starts to fall off 15 C below this value.
    double buildingTempC = 30.0;
};

struct Condenser {
    double basePressureKpa = 700.0;
    double pressurePerCapacityPct = 4.0;
    double timeConstantS = 30.0;
    double fanFailureRiseKpa = 600.0;
};

struct Limits {
    double supplyTempValidMinC = -10.0;
    double supplyTempValidMaxC = 40.0;
    double freezeProtectC = 3.5;
    double condenserPressureMaxKpa = 1400.0;
    double minFlowPct = 50.0;
    // Capacity-control check: actual capacity vs. what the command should give.
    double capacityResidualMaxPct = 15.0;
    double capacityResidualTimeS = 10.0;
};

struct Noise {
    double temperatureC = 0.0;
    double pressureKpa = 0.0;
};

struct ChillerConfig {
    std::string modelId;
    std::string type;
    std::string displayName;
    double ratedCapacityKw = 0.0;
    double cop = 1.0;
    CapacityControl capacity;
    WaterLoop water;
    Condenser condenser;
    Limits limits;
    Noise noise;
    std::map<std::string, double> modelParams;      // model-specific numbers
    std::vector<std::string> sensors;                // sensor list (informational)
    std::map<std::string, std::string> faultCodes;   // standard key -> native code
    std::string sourceFile;

    // Model-specific parameter; throws ConfigError naming the file if missing.
    double param(const std::string& name) const;
    // Native code for a standard fault, e.g. FaultCode::FreezeRisk -> "SCR-E18".
    std::string nativeCode(FaultCode code) const;

    nlohmann::json toJson() const;  // summary used by the REST API
};

ChillerConfig parseChillerConfig(const nlohmann::json& j, const std::string& sourceName);
ChillerConfig loadChillerConfig(const std::string& path);
// Loads every *.json file in a directory, sorted by file name.
std::vector<ChillerConfig> loadAllChillerConfigs(const std::string& directory);

}  // namespace chiller
