// Closed-loop tests: the one ControlApp + PID runs unchanged on every model.

#include <gtest/gtest.h>

#include <map>

#include "chiller/control_app.hpp"
#include "chiller/model_factory.hpp"
#include "chiller/runner.hpp"
#include "test_helpers.hpp"

using namespace chiller;

namespace {
// What we require from each model. A staged scroll chiller cannot hold 80 % load
// exactly with 25 % steps, so it is allowed a wider ripple.
struct Expectation {
    double maxSteadyErrorC;
    double maxPeakRiseC;
    int maxCompressorStarts;
};

const std::map<std::string, Expectation>& expectations() {
    static const std::map<std::string, Expectation> e = {
        {"screw", {0.15, 1.5, 1}},
        {"centrifugal", {0.15, 1.5, 1}},
        {"scroll", {0.8, 1.5, 12}},
    };
    return e;
}
}  // namespace

class ClosedLoop : public testing::TestWithParam<std::string> {};

TEST_P(ClosedLoop, SameControlAppHoldsSetpointOnEveryModel) {
    const std::string type = GetParam();
    const Scenario sc = testutil::defaultScenario();
    auto model = makeChiller(testutil::modelConfig(type), sc.seed);
    const RunResult r = runScenario(*model, sc);
    const Expectation& want = expectations().at(type);

    EXPECT_TRUE(r.faults.empty()) << "normal operation must not raise faults";
    ASSERT_TRUE(r.metrics.settlingTimeS.has_value()) << type << " did not settle after startup";
    EXPECT_LT(*r.metrics.settlingTimeS, sc.loadStepTimeS);
    EXPECT_LT(r.metrics.undershootC, 0.5);
    EXPECT_LT(r.metrics.peakDeviationAfterStepC, want.maxPeakRiseC);
    EXPECT_LT(r.metrics.maxSteadyErrorC, want.maxSteadyErrorC);
    EXPECT_LE(r.metrics.compressorStarts, want.maxCompressorStarts);
}

TEST_P(ClosedLoop, FollowsASetpointChange) {
    const Scenario sc = testutil::defaultScenario();
    auto model = makeChiller(testutil::modelConfig(GetParam()), sc.seed);
    ControlApp app(*model, sc.pid, 7.0);
    for (int i = 0; i < 600; ++i) app.tick(50.0, 1.0);
    app.setSetpoint(8.0);
    double sum = 0.0;
    for (int i = 0; i < 1200; ++i) {
        const auto t = app.tick(50.0, 1.0);
        if (i >= 900) sum += t.record.chwSupplyC;
    }
    EXPECT_NEAR(sum / 300.0, 8.0, 0.3);
}

INSTANTIATE_TEST_SUITE_P(AllModels, ClosedLoop, testing::ValuesIn(testutil::modelTypes()),
                         [](const testing::TestParamInfo<std::string>& info) { return info.param; });

TEST(ClosedLoop, BackCalculationHelpsTheStagedModel) {
    // Same scroll model, same gains: with actuator feedback the load-step peak is lower.
    Scenario with = testutil::defaultScenario();
    Scenario without = with;
    without.pid.actuatorFeedback = false;
    auto a = makeChiller(testutil::modelConfig("scroll"), with.seed);
    auto b = makeChiller(testutil::modelConfig("scroll"), with.seed);
    const RunMetrics mw = runScenario(*a, with).metrics;
    const RunMetrics mo = runScenario(*b, without).metrics;
    EXPECT_LT(mw.peakDeviationAfterStepC, mo.peakDeviationAfterStepC);
    EXPECT_LE(mw.compressorStarts, mo.compressorStarts);
}
