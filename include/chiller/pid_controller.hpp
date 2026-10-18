#pragma once
// Discrete PID controller for chilled-water supply temperature.
//
// Output is a capacity request in percent (0..100). It knows nothing about
// chiller models: the model decides how to deliver the requested capacity.
//
//   error = measured - setpoint        (positive = water too warm = more cooling)
//   u     = Kp*error + Ki*integral + Kd*d(measured)/dt
//
// Features:
//   * output clamping to [outMin, outMax]
//   * anti-windup by conditional integration (integral frozen while the output
//     is saturated and the error pushes it further into saturation)
//   * derivative on measurement, so a setpoint change causes no "derivative kick"
//   * actuator feedback (optional, back-calculation anti-windup): after the model
//     applies the request, the controller is told what was really commanded
//     (a scroll chiller can only give 25 % steps, a screw chiller never goes
//     below its minimum). The integral is pulled towards that value with
//     tracking time constant Tt, so the same PID works on staged and continuous
//     models without knowing which one it drives.

#include "chiller/scenario.hpp"

namespace chiller {

class PidController {
public:
    explicit PidController(PidGains gains, double outMin = 0.0, double outMax = 100.0);

    // Computes the new output. dt must be > 0.
    double update(double setpoint, double measured, double dt);
    // Tell the controller what the actuator actually applied after update().
    void applyActuatorFeedback(double appliedOutput);
    void reset();

    void setGains(PidGains gains) { gains_ = gains; }
    PidGains gains() const { return gains_; }

    // Last individual terms, useful for logging and tests.
    double lastP() const { return lastP_; }
    double lastI() const { return lastI_; }
    double lastD() const { return lastD_; }
    double integral() const { return integral_; }

private:
    PidGains gains_;
    double outMin_;
    double outMax_;
    double integral_ = 0.0;
    double prevMeasured_ = 0.0;
    bool first_ = true;
    double lastOut_ = 0.0;
    double lastDt_ = 0.0;
    double lastP_ = 0.0, lastI_ = 0.0, lastD_ = 0.0;
};

}  // namespace chiller
