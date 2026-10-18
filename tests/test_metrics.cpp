#include <gtest/gtest.h>

#include "chiller/metrics.hpp"

using namespace chiller;

namespace {
Scenario metricsScenario() {
    Scenario s;
    s.setpointC = 7.0;
    s.dtS = 1.0;
    s.settleBandC = 0.25;
    s.loadStepTimeS = 10.0;
    s.steadyWindowS = 5.0;
    return s;
}

std::vector<BlackBoxRecord> series(const std::vector<double>& temps) {
    std::vector<BlackBoxRecord> out;
    for (std::size_t i = 0; i < temps.size(); ++i) {
        BlackBoxRecord r;
        r.timeS = static_cast<double>(i);
        r.chwSupplyC = temps[i];
        r.powerKw = 36.0;
        out.push_back(r);
    }
    return out;
}
}  // namespace

TEST(Metrics, StartupAndLoadStepFigures) {
    //            t: 0    1    2    3    4    5    6    7    8    9  | 10   11   12   13   14   15   16   17   18   19
    const auto recs = series({12.0, 10.0, 8.0, 6.8, 6.9, 7.1, 7.0, 7.0, 7.1, 7.0,
                              7.0, 7.6, 7.9, 7.5, 7.2, 7.1, 7.0, 7.0, 7.05, 6.95});
    const RunMetrics m = computeMetrics(recs, metricsScenario(), 1);
    EXPECT_NEAR(m.undershootC, 0.2, 1e-9);         // min 6.8 before the step
    ASSERT_TRUE(m.settlingTimeS.has_value());
    EXPECT_DOUBLE_EQ(*m.settlingTimeS, 3.0);       // last outside the band at t=2
    EXPECT_NEAR(m.peakDeviationAfterStepC, 0.9, 1e-9);
    ASSERT_TRUE(m.recoveryTimeS.has_value());
    EXPECT_DOUBLE_EQ(*m.recoveryTimeS, 4.0);       // last outside at t=13 -> back at 14, step at 10
    EXPECT_NEAR(m.maxSteadyErrorC, 0.1, 1e-9);        // 7.1 at t=15
    EXPECT_NEAR(m.energyKwh, 20 * 36.0 / 3600.0, 1e-9);
    EXPECT_EQ(m.compressorStarts, 1);
}

TEST(Metrics, NotReachedWhenItNeverSettles) {
    const auto recs = series({12, 11, 10, 9, 8.5, 8.4, 8.3, 8.2, 8.1, 8.0, 9, 9, 9, 9, 9});
    const RunMetrics m = computeMetrics(recs, metricsScenario(), 0);
    EXPECT_FALSE(m.settlingTimeS.has_value());
    EXPECT_FALSE(m.recoveryTimeS.has_value());
}

TEST(Metrics, EmptyInputGivesZeroes) {
    const RunMetrics m = computeMetrics({}, metricsScenario(), 0);
    EXPECT_EQ(m.samples, 0);
    EXPECT_DOUBLE_EQ(m.energyKwh, 0.0);
}
