#include <gtest/gtest.h>

#include <fstream>
#include <set>

#include "chiller/config.hpp"
#include "chiller/model_factory.hpp"
#include "chiller/models.hpp"
#include "chiller/scenario.hpp"
#include "test_helpers.hpp"

using namespace chiller;
using nlohmann::json;

namespace {
json screwJson() {
    std::ifstream in(testutil::modelsDir() + "/screw.json");
    json j;
    in >> j;
    return j;
}

std::string errorMessage(const json& j) {
    try {
        parseChillerConfig(j, "test.json");
    } catch (const ConfigError& e) {
        return e.what();
    }
    return "";
}
}  // namespace

TEST(Config, LoadsAllShippedModels) {
    const auto configs = loadAllChillerConfigs(testutil::modelsDir());
    ASSERT_EQ(configs.size(), 3u);
    std::set<std::string> types, ids;
    for (const auto& c : configs) {
        types.insert(c.type);
        ids.insert(c.modelId);
        EXPECT_GT(c.ratedCapacityKw, 0.0);
        EXPECT_FALSE(c.sensors.empty());
    }
    EXPECT_EQ(types, (std::set<std::string>{"screw", "centrifugal", "scroll"}));
    EXPECT_EQ(ids.size(), 3u) << "model ids must be unique";
}

TEST(Config, EveryModelMapsEveryStandardFaultToItsOwnCode) {
    for (const auto& c : loadAllChillerConfigs(testutil::modelsDir())) {
        for (FaultCode code : {FaultCode::SensorFailure, FaultCode::HighCondenserPressure,
                               FaultCode::LowChilledWaterFlow, FaultCode::FreezeRisk,
                               FaultCode::CompressorProtection, FaultCode::CapacityControlFault}) {
            const std::string native = c.nativeCode(code);
            EXPECT_FALSE(native.empty());
            EXPECT_NE(native, toString(code)) << c.modelId << " has no native code for " << toString(code);
        }
    }
}

TEST(Config, MissingFieldErrorNamesTheField) {
    json j = screwJson();
    j.erase("cop");
    const std::string msg = errorMessage(j);
    EXPECT_NE(msg.find("cop"), std::string::npos) << msg;
    EXPECT_NE(msg.find("test.json"), std::string::npos) << msg;
}

TEST(Config, MissingFaultCodeIsRejected) {
    json j = screwJson();
    j["fault_codes"].erase("freeze_risk");
    EXPECT_NE(errorMessage(j).find("freeze_risk"), std::string::npos);
}

TEST(Config, WrongTypeIsRejected) {
    json j = screwJson();
    j["cop"] = "very high";
    EXPECT_NE(errorMessage(j).find("wrong type"), std::string::npos);
}

TEST(Config, OutOfRangeValueIsRejected) {
    json j = screwJson();
    j["capacity_control"]["min_capacity_pct"] = 120;
    EXPECT_NE(errorMessage(j).find("min_capacity_pct"), std::string::npos);
}

TEST(Config, InvalidJsonFileIsReported) {
    const std::string path = testing::TempDir() + "broken_model.json";
    std::ofstream(path) << "{ \"model_id\": ";
    EXPECT_THROW(loadChillerConfig(path), ConfigError);
}

TEST(Factory, CreatesTheRightModelForEachType) {
    for (const auto& c : loadAllChillerConfigs(testutil::modelsDir())) {
        auto model = makeChiller(c);
        EXPECT_EQ(model->type(), c.type);
        EXPECT_EQ(model->config().modelId, c.modelId);
    }
}

TEST(Factory, UnknownTypeIsRejected) {
    ChillerConfig c = testutil::modelConfig("screw");
    c.type = "magnetic_bearing";
    EXPECT_THROW(makeChiller(c), ConfigError);
}

TEST(Factory, MissingModelParameterIsReportedAtConstruction) {
    ChillerConfig c = testutil::modelConfig("screw");
    c.modelParams.erase("oil_pressure_min_kpa");
    try {
        makeChiller(c);
        FAIL() << "expected ConfigError";
    } catch (const ConfigError& e) {
        EXPECT_NE(std::string(e.what()).find("oil_pressure_min_kpa"), std::string::npos);
    }
}

TEST(Factory, NewModelTypesCanBeRegistered) {
    // Adding a model = one class + one registration line + one JSON file.
    registerChillerType("screw_variant", [](const ChillerConfig& c, std::uint32_t seed) {
        return std::make_unique<ScrewChiller>(c, seed);
    });
    ChillerConfig c = testutil::modelConfig("screw");
    c.type = "screw_variant";
    EXPECT_NO_THROW(makeChiller(c));
}

TEST(Scenario, LoadsShippedScenario) {
    const Scenario s = testutil::defaultScenario();
    EXPECT_DOUBLE_EQ(s.setpointC, 7.0);
    EXPECT_GT(s.pid.kp, 0.0);
    EXPECT_GT(s.durationS, s.loadStepTimeS);
}

TEST(Scenario, LoadProfileIsAStepFunction) {
    Scenario s;
    s.loadProfile = {{0.0, 50.0}, {600.0, 80.0}};
    EXPECT_DOUBLE_EQ(s.loadPctAt(0.0), 50.0);
    EXPECT_DOUBLE_EQ(s.loadPctAt(599.0), 50.0);
    EXPECT_DOUBLE_EQ(s.loadPctAt(600.0), 80.0);
    EXPECT_DOUBLE_EQ(s.loadPctAt(5000.0), 80.0);
}
