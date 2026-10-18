#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <tuple>

#include "chiller/control_app.hpp"
#include "chiller/model_factory.hpp"
#include "chiller/runner.hpp"
#include "test_helpers.hpp"

using namespace chiller;

// ---------- Limit checks on hand-made readings ----------

namespace {
SensorData healthyReading() {
    SensorData s;
    s.chwSupplyTempC = 7.0;
    s.chwReturnTempC = 12.0;
    s.chwFlowPct = 100.0;
    s.condenserPressureKpa = 1000.0;
    return s;
}

bool contains(const std::vector<DetectedFault>& faults, FaultCode code) {
    for (const auto& f : faults) {
        if (f.code == code) return true;
    }
    return false;
}
}  // namespace

TEST(FaultChecks, HealthyReadingRaisesNothing) {
    auto model = makeChiller(testutil::modelConfig("screw"));
    SensorData s = healthyReading();
    s.extra["oil_pressure_kpa"] = 320.0;
    EXPECT_TRUE(model->checkFaults(s).empty());
}

TEST(FaultChecks, FreezeRiskBelowLimit) {
    auto model = makeChiller(testutil::modelConfig("screw"));
    SensorData s = healthyReading();
    s.extra["oil_pressure_kpa"] = 320.0;
    s.chwSupplyTempC = 3.0;  // limit 3.5 C
    const auto faults = model->checkFaults(s);
    ASSERT_EQ(faults.size(), 1u);
    EXPECT_EQ(faults[0].code, FaultCode::FreezeRisk);
    EXPECT_EQ(faults[0].nativeCode, "SCR-E18");
}

TEST(FaultChecks, BrokenSensorIsNotMistakenForFreezing) {
    auto model = makeChiller(testutil::modelConfig("screw"));
    SensorData s = healthyReading();
    s.extra["oil_pressure_kpa"] = 320.0;
    s.chwSupplyTempC = -40.0;  // open-circuit reading
    const auto faults = model->checkFaults(s);
    EXPECT_TRUE(contains(faults, FaultCode::SensorFailure));
    EXPECT_FALSE(contains(faults, FaultCode::FreezeRisk));
}

TEST(FaultChecks, NaNReadingIsASensorFailure) {
    auto model = makeChiller(testutil::modelConfig("centrifugal"));
    SensorData s = healthyReading();
    s.extra["surge_margin_pct"] = 20.0;
    s.chwSupplyTempC = std::nan("");
    EXPECT_TRUE(contains(model->checkFaults(s), FaultCode::SensorFailure));
}

TEST(FaultChecks, SameStandardCodeDifferentNativeCodes) {
    // The point of the standard format: F02 means the same thing on every model.
    std::set<std::string> natives;
    for (const auto& type : testutil::modelTypes()) {
        auto model = makeChiller(testutil::modelConfig(type));
        SensorData s = healthyReading();
        s.extra = model->readSensors().extra;
        s.condenserPressureKpa = 2000.0;
        const auto faults = model->checkFaults(s);
        ASSERT_TRUE(contains(faults, FaultCode::HighCondenserPressure)) << type;
        natives.insert(faults[0].nativeCode);
    }
    EXPECT_EQ(natives.size(), 3u);
}

// ---------- Fault-injection campaign: every fault on every model ----------

class FaultInjection : public testing::TestWithParam<std::tuple<std::string, InjectedFault>> {};

TEST_P(FaultInjection, DetectedWithCorrectCodeAndChillerTrips) {
    const auto& [type, fault] = GetParam();
    const ChillerConfig cfg = testutil::modelConfig(type);
    const Scenario sc = testutil::defaultScenario();
    const FaultTrial trial = runFaultTrial(cfg, sc, fault);

    EXPECT_TRUE(trial.detected) << toString(fault) << " not detected on " << type;
    EXPECT_EQ(trial.firstCode, expectedCode(fault)) << "first fault raised was " << toString(trial.firstCode);
    EXPECT_TRUE(trial.tripped);
    EXPECT_EQ(trial.nativeCode, cfg.nativeCode(expectedCode(fault)));
    ASSERT_TRUE(trial.latencyS.has_value());
    EXPECT_LE(*trial.latencyS, 60.0) << "detection took too long";
}

INSTANTIATE_TEST_SUITE_P(
    AllModelsAllFaults, FaultInjection,
    testing::Combine(testing::ValuesIn(testutil::modelTypes()), testing::ValuesIn(allInjectableFaults())),
    [](const testing::TestParamInfo<std::tuple<std::string, InjectedFault>>& info) {
        return std::get<0>(info.param) + "_" + toString(std::get<1>(info.param));
    });

class NoFalseAlarms : public testing::TestWithParam<std::string> {};

TEST_P(NoFalseAlarms, NormalScenarioRaisesNoFault) {
    EXPECT_EQ(countFalseAlarms(testutil::modelConfig(GetParam()), testutil::defaultScenario()), 0);
}

INSTANTIATE_TEST_SUITE_P(AllModels, NoFalseAlarms, testing::ValuesIn(testutil::modelTypes()),
                         [](const testing::TestParamInfo<std::string>& info) { return info.param; });

// ---------- Trip latch and operator reset ----------

TEST(ControlAppFaults, FaultStaysLatchedUntilOperatorReset) {
    const Scenario sc = testutil::defaultScenario();
    auto model = makeChiller(testutil::modelConfig("screw"), sc.seed);
    ControlApp app(*model, sc.pid, sc.setpointC);

    for (int i = 0; i < 300; ++i) app.tick(50.0, 1.0);
    model->injectFault(InjectedFault::PumpFailure);
    TickResult last;
    for (int i = 0; i < 30; ++i) last = app.tick(50.0, 1.0);
    EXPECT_EQ(model->state(), ChillerState::Tripped);
    EXPECT_EQ(last.record.faultCode, "F03");
    EXPECT_EQ(last.record.nativeFaultCode, "SCR-E15");
    EXPECT_DOUBLE_EQ(last.record.capacityCommandPct, 0.0);

    app.resetAlarms();  // technician fixed the pump
    EXPECT_TRUE(app.activeFaults().empty());
    for (int i = 0; i < 30; ++i) last = app.tick(50.0, 1.0);
    EXPECT_EQ(model->state(), ChillerState::Running);
    EXPECT_GT(last.record.capacityCommandPct, 0.0);
    EXPECT_EQ(last.record.faultCode, "");
}

TEST(ControlAppFaults, EachFaultIsReportedOnceUntilReset) {
    const Scenario sc = testutil::defaultScenario();
    auto model = makeChiller(testutil::modelConfig("centrifugal"), sc.seed);
    ControlApp app(*model, sc.pid, sc.setpointC);
    model->injectFault(InjectedFault::SupplySensorFailure);
    int raised = 0;
    for (int i = 0; i < 50; ++i) raised += static_cast<int>(app.tick(50.0, 1.0).newFaults.size());
    EXPECT_EQ(raised, 1);
}
