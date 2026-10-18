#include <gtest/gtest.h>

#include <cmath>
#include <set>

#include "chiller/control_app.hpp"
#include "chiller/model_factory.hpp"
#include "chiller/models.hpp"
#include "chiller/runner.hpp"
#include "test_helpers.hpp"

using namespace chiller;

// ---------- Screw: smooth capacity with a minimum ----------

TEST(ScrewChiller, NeverRunsBelowMinimumAndCyclesOffAtVeryLowRequest) {
    ScrewChiller c(testutil::modelConfig("screw"));
    c.setCapacity(50.0);
    EXPECT_DOUBLE_EQ(c.commandedCapacityPct(), 50.0);
    c.setCapacity(20.0);  // below the 25 % minimum: held at minimum
    EXPECT_DOUBLE_EQ(c.commandedCapacityPct(), 25.0);
    c.setCapacity(10.0);  // below half the minimum: cycles off
    EXPECT_DOUBLE_EQ(c.commandedCapacityPct(), 0.0);
    c.setCapacity(20.0);  // hysteresis: does not restart until the minimum is requested
    EXPECT_DOUBLE_EQ(c.commandedCapacityPct(), 0.0);
    c.setCapacity(30.0);
    EXPECT_DOUBLE_EQ(c.commandedCapacityPct(), 30.0);
    EXPECT_EQ(c.compressorStarts(), 2);
}

TEST(ScrewChiller, CapacityFollowsCommandWithFirstOrderLag) {
    ScrewChiller c(testutil::modelConfig("screw"));  // response time 20 s
    c.setCapacity(100.0);
    for (int i = 0; i < 20; ++i) c.step(1.0, 0.0);
    // Discrete first-order lag after one time constant: 1 - (1 - 1/20)^20 = 64 %
    EXPECT_NEAR(c.actualCapacityPct(), 100.0 * (1.0 - std::pow(1.0 - 1.0 / 20.0, 20)), 1e-9);
    for (int i = 0; i < 200; ++i) c.step(1.0, 0.0);
    EXPECT_NEAR(c.actualCapacityPct(), 100.0, 0.01);
}

// ---------- Centrifugal: higher minimum for surge protection ----------

TEST(CentrifugalChiller, MinimumCapacityProtectsAgainstSurge) {
    CentrifugalChiller c(testutil::modelConfig("centrifugal"));
    c.setCapacity(80.0);
    c.setCapacity(20.0);
    EXPECT_DOUBLE_EQ(c.commandedCapacityPct(), 30.0);
}

TEST(CentrifugalChiller, SurgeMarginGrowsWithLoad) {
    CentrifugalChiller c(testutil::modelConfig("centrifugal"));
    c.setCapacity(30.0);
    for (int i = 0; i < 300; ++i) c.step(1.0, 600.0);
    const double lowLoadMargin = c.readSensors().extra.at("surge_margin_pct");
    c.setCapacity(100.0);
    for (int i = 0; i < 300; ++i) c.step(1.0, 2000.0);
    EXPECT_GT(c.readSensors().extra.at("surge_margin_pct"), lowLoadMargin);
}

// ---------- Scroll: discrete compressor stages ----------

TEST(ScrollChiller, DeliversOnlyWholeStages) {
    ScrollChiller c(testutil::modelConfig("scroll"));
    const std::set<double> allowed = {0.0, 25.0, 50.0, 75.0, 100.0};
    for (int i = 0; i < 2000; ++i) {
        c.setCapacity(std::fmod(i * 7.3, 100.0));
        EXPECT_TRUE(allowed.count(c.commandedCapacityPct())) << c.commandedCapacityPct();
        c.step(1.0, 200.0);
    }
}

TEST(ScrollChiller, WaitsMinimumIntervalBetweenStageChanges) {
    ScrollChiller c(testutil::modelConfig("scroll"));  // 60 s between switches
    c.setCapacity(100.0);
    EXPECT_EQ(c.activeStages(), 1);  // one compressor at a time
    for (int i = 0; i < 59; ++i) {
        c.step(1.0, 0.0);
        c.setCapacity(100.0);
    }
    EXPECT_EQ(c.activeStages(), 1);
    c.step(1.0, 0.0);
    c.setCapacity(100.0);
    EXPECT_EQ(c.activeStages(), 2);
}

TEST(ScrollChiller, DeadbandPreventsChatteringNearAStage) {
    ScrollChiller c(testutil::modelConfig("scroll"));  // +10 % to add, -15 % to drop
    for (int k = 0; k < 2; ++k) {                    // bring it to 2 stages (50 %)
        c.setCapacity(100.0);
        for (int i = 0; i < 60; ++i) c.step(1.0, 0.0);
    }
    c.setCapacity(55.0);
    ASSERT_EQ(c.activeStages(), 2);
    for (int i = 0; i < 300; ++i) {
        c.setCapacity(i % 2 ? 58.0 : 40.0);  // wiggles inside the deadband [35, 60]
        c.step(1.0, 0.0);
    }
    EXPECT_EQ(c.activeStages(), 2);
    EXPECT_EQ(c.compressorStarts(), 2);
}

TEST(ScrollChiller, TripStopsAllCompressors) {
    ScrollChiller c(testutil::modelConfig("scroll"));
    c.setCapacity(100.0);
    c.trip();
    EXPECT_EQ(c.activeStages(), 0);
    c.setCapacity(100.0);
    EXPECT_DOUBLE_EQ(c.commandedCapacityPct(), 0.0);
}

// ---------- Behaviour every model must share ----------

class EveryModel : public testing::TestWithParam<std::string> {};

TEST_P(EveryModel, ReportsCommonAndModelSpecificSensors) {
    const auto cfg = testutil::modelConfig(GetParam());
    auto model = makeChiller(cfg);
    const SensorData s = model->readSensors();
    EXPECT_NEAR(s.chwSupplyTempC, cfg.water.initialSupplyTempC, 0.2);
    EXPECT_DOUBLE_EQ(s.chwFlowPct, 100.0);
    EXPECT_EQ(s.extra.size(), 2u) << "each model adds two model-specific sensors";
}

TEST_P(EveryModel, WaterCoolsWhenCapacityExceedsLoadAndWarmsOtherwise) {
    const auto cfg = testutil::modelConfig(GetParam());
    auto model = makeChiller(cfg);
    auto* sim = dynamic_cast<SimulatedChiller*>(model.get());
    ASSERT_NE(sim, nullptr);
    const double start = sim->trueSupplyTempC();
    for (int i = 0; i < 120; ++i) {
        model->setCapacity(100.0);
        model->step(1.0, 0.3 * cfg.ratedCapacityKw);
    }
    EXPECT_LT(sim->trueSupplyTempC(), start);
    model->trip();
    const double cooled = sim->trueSupplyTempC();
    for (int i = 0; i < 120; ++i) model->step(1.0, 0.3 * cfg.ratedCapacityKw);
    EXPECT_GT(sim->trueSupplyTempC(), cooled);
}

TEST_P(EveryModel, WaterNeverWarmsAboveBuildingTemperature) {
    // A tripped chiller under full load: the loop drifts up to building air
    // temperature and stops there, so the reading stays a valid sensor value.
    const auto cfg = testutil::modelConfig(GetParam());
    auto model = makeChiller(cfg);
    model->trip();
    for (int i = 0; i < 20000; ++i) model->step(1.0, cfg.ratedCapacityKw);
    const SensorData s = model->readSensors();
    EXPECT_LE(s.chwSupplyTempC, cfg.water.buildingTempC + 0.1);
    EXPECT_GT(s.chwSupplyTempC, cfg.water.buildingTempC - 1.0);
    for (const auto& f : model->checkFaults(s)) EXPECT_NE(f.code, FaultCode::SensorFailure);
}

TEST_P(EveryModel, TripForcesZeroCapacityUntilFaultsAreCleared) {
    auto model = makeChiller(testutil::modelConfig(GetParam()));
    model->setCapacity(100.0);
    model->trip();
    EXPECT_EQ(model->state(), ChillerState::Tripped);
    EXPECT_DOUBLE_EQ(model->commandedCapacityPct(), 0.0);
    model->setCapacity(100.0);
    EXPECT_DOUBLE_EQ(model->commandedCapacityPct(), 0.0);
    model->clearFaults();
    EXPECT_EQ(model->state(), ChillerState::Running);
}

TEST_P(EveryModel, SameSeedGivesIdenticalRuns) {
    const auto cfg = testutil::modelConfig(GetParam());
    const Scenario sc = testutil::defaultScenario();
    auto a = makeChiller(cfg, 7);
    auto b = makeChiller(cfg, 7);
    const auto ra = runScenario(*a, sc).records;
    const auto rb = runScenario(*b, sc).records;
    ASSERT_EQ(ra.size(), rb.size());
    for (std::size_t i = 0; i < ra.size(); ++i) ASSERT_DOUBLE_EQ(ra[i].chwSupplyC, rb[i].chwSupplyC);
}

TEST_P(EveryModel, ResetRestoresInitialConditions) {
    const auto cfg = testutil::modelConfig(GetParam());
    auto model = makeChiller(cfg);
    model->setCapacity(100.0);
    for (int i = 0; i < 100; ++i) model->step(1.0, 0.0);
    model->injectFault(InjectedFault::PumpFailure);
    model->reset();
    EXPECT_DOUBLE_EQ(model->simTimeS(), 0.0);
    EXPECT_EQ(model->compressorStarts(), 0);
    EXPECT_DOUBLE_EQ(model->readSensors().chwFlowPct, 100.0);
}

INSTANTIATE_TEST_SUITE_P(AllModels, EveryModel, testing::ValuesIn(testutil::modelTypes()),
                         [](const testing::TestParamInfo<std::string>& info) { return info.param; });
