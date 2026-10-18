#include <gtest/gtest.h>

#include <stdexcept>

#include "chiller/pid_controller.hpp"

using chiller::PidController;
using chiller::PidGains;

namespace {
PidGains gains(double kp, double ki, double kd) { return PidGains{kp, ki, kd, true, 60.0}; }
}  // namespace

TEST(PidController, ProportionalOnlyOutputIsKpTimesError) {
    PidController pid(gains(25.0, 0.0, 0.0));
    // Water 2 C too warm -> 50 % capacity.
    EXPECT_DOUBLE_EQ(pid.update(7.0, 9.0, 1.0), 50.0);
    EXPECT_DOUBLE_EQ(pid.lastP(), 50.0);
}

TEST(PidController, ZeroErrorGivesZeroOutputWithoutIntegral) {
    PidController pid(gains(25.0, 0.0, 0.0));
    EXPECT_DOUBLE_EQ(pid.update(7.0, 7.0, 1.0), 0.0);
}

TEST(PidController, OutputIsClampedToRange) {
    PidController pid(gains(25.0, 0.0, 0.0));
    EXPECT_DOUBLE_EQ(pid.update(7.0, 100.0, 1.0), 100.0);  // huge positive error
    EXPECT_DOUBLE_EQ(pid.update(7.0, -50.0, 1.0), 0.0);    // huge negative error
}

TEST(PidController, ColdWaterAsksForLessCapacity) {
    // Reverse acting: water colder than setpoint must never ask for capacity.
    PidController pid(gains(25.0, 0.0, 0.0));
    EXPECT_DOUBLE_EQ(pid.update(7.0, 6.0, 1.0), 0.0);
}

TEST(PidController, IntegralAccumulatesConstantError) {
    PidController pid(gains(0.0, 1.0, 0.0));
    EXPECT_DOUBLE_EQ(pid.update(7.0, 8.0, 1.0), 0.0);  // integral used before it is updated
    EXPECT_DOUBLE_EQ(pid.update(7.0, 8.0, 1.0), 1.0);
    EXPECT_DOUBLE_EQ(pid.update(7.0, 8.0, 1.0), 2.0);
    EXPECT_DOUBLE_EQ(pid.integral(), 3.0);
}

TEST(PidController, IntegralRemovesSteadyStateOffset) {
    // With a constant 0.5 C error, PI keeps increasing output; P alone would stay at 12.5.
    PidController p(gains(25.0, 0.0, 0.0));
    PidController pi(gains(25.0, 0.15, 0.0));
    double outP = 0.0, outPI = 0.0;
    for (int i = 0; i < 100; ++i) {
        outP = p.update(7.0, 7.5, 1.0);
        outPI = pi.update(7.0, 7.5, 1.0);
    }
    EXPECT_DOUBLE_EQ(outP, 12.5);
    EXPECT_GT(outPI, 12.5 + 5.0);
}

TEST(PidController, AntiWindupFreezesIntegralWhileSaturated) {
    PidController pid(gains(50.0, 1.0, 0.0));
    for (int i = 0; i < 200; ++i) pid.update(7.0, 12.0, 1.0);  // 5 C error: output pinned at 100 %
    EXPECT_DOUBLE_EQ(pid.integral(), 0.0);
    // As soon as the water reaches setpoint the output drops right away (no wound-up integral).
    EXPECT_DOUBLE_EQ(pid.update(7.0, 7.0, 1.0), 0.0);
}

TEST(PidController, DerivativeOnMeasurementHasNoSetpointKick) {
    PidController pid(gains(0.0, 0.0, 10.0));
    pid.update(7.0, 8.0, 1.0);
    // Setpoint jumps from 7 to 5 while the measurement stays at 8: no derivative output.
    EXPECT_DOUBLE_EQ(pid.update(5.0, 8.0, 1.0), 0.0);
    EXPECT_DOUBLE_EQ(pid.lastD(), 0.0);
}

TEST(PidController, DerivativeReactsToMeasurementChange) {
    PidController pid(gains(0.0, 0.0, 10.0));
    pid.update(7.0, 8.0, 1.0);
    EXPECT_DOUBLE_EQ(pid.update(7.0, 9.0, 1.0), 10.0);  // +1 C/s * Kd 10
}

TEST(PidController, BackCalculationPullsIntegralTowardsAppliedOutput) {
    PidController pid(PidGains{0.0, 0.5, 0.0, true, 10.0});
    for (int i = 0; i < 20; ++i) pid.update(7.0, 9.0, 1.0);  // build up integral (output 0..20)
    const double before = pid.integral();
    const double out = pid.update(7.0, 9.0, 1.0);
    const double afterUpdate = pid.integral();
    EXPECT_DOUBLE_EQ(afterUpdate - before, 2.0);  // normal integration of error 2 for 1 s
    pid.applyActuatorFeedback(out - 10.0);        // actuator delivered 10 % less
    // integral changes by (applied - out) / (Ki * Tt) * dt = -10 / 5 = -2
    EXPECT_NEAR(pid.integral(), afterUpdate - 2.0, 1e-12);
}

TEST(PidController, BackCalculationDoesNothingWhenActuatorFollows) {
    PidController pid(PidGains{25.0, 0.15, 0.0, true, 60.0});
    const double out = pid.update(7.0, 8.0, 1.0);
    const double integral = pid.integral();
    pid.applyActuatorFeedback(out);
    EXPECT_DOUBLE_EQ(pid.integral(), integral);
}

TEST(PidController, ResetClearsState) {
    PidController pid(gains(10.0, 1.0, 5.0));
    for (int i = 0; i < 10; ++i) pid.update(7.0, 7.0 + i * 0.1, 1.0);
    pid.reset();
    EXPECT_DOUBLE_EQ(pid.integral(), 0.0);
    EXPECT_DOUBLE_EQ(pid.update(7.0, 8.0, 1.0), 10.0);  // no derivative on the first tick after reset
}

TEST(PidController, RejectsInvalidArguments) {
    EXPECT_THROW(PidController(gains(1, 0, 0), 100.0, 0.0), std::invalid_argument);
    PidController pid(gains(1, 0, 0));
    EXPECT_THROW(pid.update(7.0, 8.0, 0.0), std::invalid_argument);
}
