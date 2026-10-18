#include "chiller/config.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace chiller {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

// Reads a required field and turns any JSON error into a readable ConfigError.
template <typename T>
T require(const json& j, const std::string& key, const std::string& where) {
    if (!j.contains(key)) {
        throw ConfigError(where + ": missing required field '" + key + "'");
    }
    try {
        return j.at(key).get<T>();
    } catch (const json::exception&) {
        throw ConfigError(where + ": field '" + key + "' has the wrong type");
    }
}

// Takes const char* (not std::string) so no temporary is bound to a reference parameter.
const json& requireObject(const json& j, const char* key, const std::string& where) {
    if (!j.contains(key) || !j.at(key).is_object()) {
        throw ConfigError(where + ": missing required object '" + std::string(key) + "'");
    }
    return j.at(key);
}

void check(bool ok, const std::string& where, const std::string& message) {
    if (!ok) throw ConfigError(where + ": " + message);
}

}  // namespace

double ChillerConfig::param(const std::string& name) const {
    auto it = modelParams.find(name);
    if (it == modelParams.end()) {
        throw ConfigError(sourceFile + ": missing model_params." + name);
    }
    return it->second;
}

std::string ChillerConfig::nativeCode(FaultCode code) const {
    auto it = faultCodes.find(configKey(code));
    return it == faultCodes.end() ? toString(code) : it->second;
}

json ChillerConfig::toJson() const {
    return json{{"model_id", modelId},
                {"type", type},
                {"display_name", displayName},
                {"rated_capacity_kw", ratedCapacityKw},
                {"cop", cop},
                {"capacity_method", capacity.method},
                {"min_capacity_pct", capacity.minCapacityPct},
                {"sensors", sensors},
                {"fault_codes", faultCodes}};
}

ChillerConfig parseChillerConfig(const json& j, const std::string& where) {
    check(j.is_object(), where, "top level must be a JSON object");

    ChillerConfig c;
    c.sourceFile = where;
    c.modelId = require<std::string>(j, "model_id", where);
    c.type = require<std::string>(j, "type", where);
    c.displayName = j.value("display_name", c.modelId);
    c.ratedCapacityKw = require<double>(j, "rated_capacity_kw", where);
    c.cop = require<double>(j, "cop", where);

    const json& cap = requireObject(j, "capacity_control", where);
    c.capacity.method = require<std::string>(cap, "method", where + " capacity_control");
    c.capacity.minCapacityPct = require<double>(cap, "min_capacity_pct", where + " capacity_control");
    c.capacity.responseTimeS = require<double>(cap, "response_time_s", where + " capacity_control");

    const json& w = requireObject(j, "water_loop", where);
    c.water.thermalMassKjPerC = require<double>(w, "thermal_mass_kj_per_c", where + " water_loop");
    c.water.nominalFlowKgS = require<double>(w, "nominal_flow_kg_s", where + " water_loop");
    c.water.initialSupplyTempC = require<double>(w, "initial_supply_temp_c", where + " water_loop");
    c.water.flowTimeConstantS = w.value("flow_time_constant_s", 5.0);
    c.water.buildingTempC = w.value("building_temp_c", 30.0);

    const json& cond = requireObject(j, "condenser", where);
    c.condenser.basePressureKpa = require<double>(cond, "base_pressure_kpa", where + " condenser");
    c.condenser.pressurePerCapacityPct =
        require<double>(cond, "pressure_per_capacity_pct", where + " condenser");
    c.condenser.timeConstantS = require<double>(cond, "time_constant_s", where + " condenser");
    c.condenser.fanFailureRiseKpa = cond.value("fan_failure_rise_kpa", 600.0);

    const json& lim = requireObject(j, "limits", where);
    c.limits.supplyTempValidMinC = require<double>(lim, "supply_temp_valid_min_c", where + " limits");
    c.limits.supplyTempValidMaxC = require<double>(lim, "supply_temp_valid_max_c", where + " limits");
    c.limits.freezeProtectC = require<double>(lim, "freeze_protect_c", where + " limits");
    c.limits.condenserPressureMaxKpa =
        require<double>(lim, "condenser_pressure_max_kpa", where + " limits");
    c.limits.minFlowPct = require<double>(lim, "min_flow_pct", where + " limits");
    c.limits.capacityResidualMaxPct = lim.value("capacity_residual_max_pct", c.limits.capacityResidualMaxPct);
    c.limits.capacityResidualTimeS = lim.value("capacity_residual_time_s", c.limits.capacityResidualTimeS);

    if (j.contains("noise")) {
        c.noise.temperatureC = j.at("noise").value("temperature_c", 0.0);
        c.noise.pressureKpa = j.at("noise").value("pressure_kpa", 0.0);
    }

    if (j.contains("model_params")) {
        for (const auto& [key, value] : j.at("model_params").items()) {
            check(value.is_number(), where, "model_params." + key + " must be a number");
            c.modelParams[key] = value.get<double>();
        }
    }
    if (j.contains("sensors")) c.sensors = j.at("sensors").get<std::vector<std::string>>();

    const json& codes = requireObject(j, "fault_codes", where);
    for (FaultCode code : {FaultCode::SensorFailure, FaultCode::HighCondenserPressure,
                           FaultCode::LowChilledWaterFlow, FaultCode::FreezeRisk,
                           FaultCode::CompressorProtection, FaultCode::CapacityControlFault}) {
        c.faultCodes[configKey(code)] = require<std::string>(codes, configKey(code), where + " fault_codes");
    }

    // Sanity checks: catch typos before they turn into strange simulation results.
    check(c.ratedCapacityKw > 0, where, "rated_capacity_kw must be > 0");
    check(c.cop > 0, where, "cop must be > 0");
    check(c.capacity.minCapacityPct >= 0 && c.capacity.minCapacityPct < 100, where,
          "min_capacity_pct must be in [0, 100)");
    check(c.capacity.responseTimeS > 0, where, "response_time_s must be > 0");
    check(c.water.thermalMassKjPerC > 0, where, "thermal_mass_kj_per_c must be > 0");
    check(c.water.nominalFlowKgS > 0, where, "nominal_flow_kg_s must be > 0");
    check(c.condenser.timeConstantS > 0, where, "condenser time_constant_s must be > 0");
    check(c.limits.supplyTempValidMinC < c.limits.supplyTempValidMaxC, where,
          "supply temperature valid range is empty");
    check(c.limits.minFlowPct > 0 && c.limits.minFlowPct < 100, where, "min_flow_pct must be in (0, 100)");
    check(c.limits.capacityResidualMaxPct > 0, where, "capacity_residual_max_pct must be > 0");
    return c;
}

ChillerConfig loadChillerConfig(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw ConfigError("cannot open config file: " + path);
    json j;
    try {
        in >> j;
    } catch (const json::parse_error& e) {
        throw ConfigError(path + ": invalid JSON: " + e.what());
    }
    return parseChillerConfig(j, path);
}

std::vector<ChillerConfig> loadAllChillerConfigs(const std::string& directory) {
    if (!fs::is_directory(directory)) throw ConfigError("not a directory: " + directory);
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    std::vector<ChillerConfig> configs;
    for (const auto& f : files) configs.push_back(loadChillerConfig(f.string()));
    if (configs.empty()) throw ConfigError("no model configs (*.json) found in " + directory);
    return configs;
}

}  // namespace chiller
