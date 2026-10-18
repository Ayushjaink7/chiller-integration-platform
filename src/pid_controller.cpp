#include "chiller/pid_controller.hpp"

#include <algorithm>
#include <stdexcept>

namespace chiller {

PidController::PidController(PidGains gains, double outMin, double outMax)
    : gains_(gains), outMin_(outMin), outMax_(outMax) {
    if (outMin >= outMax) throw std::invalid_argument("PID output range is empty");
}

double PidController::update(double setpoint, double measured, double dt) {
    if (dt <= 0.0) throw std::invalid_argument("PID dt must be > 0");

    const double error = measured - setpoint;  // reverse-acting: too warm -> more cooling
    const double dMeasured = first_ ? 0.0 : (measured - prevMeasured_) / dt;
    first_ = false;
    prevMeasured_ = measured;

    lastP_ = gains_.kp * error;
    lastI_ = gains_.ki * integral_;
    lastD_ = gains_.kd * dMeasured;  // derivative on measurement: no kick on setpoint change

    const double raw = lastP_ + lastI_ + lastD_;
    const double out = std::clamp(raw, outMin_, outMax_);

    // Anti-windup: only integrate when it would not push a saturated output further.
    const bool pushingHigh = raw > outMax_ && error > 0.0;
    const bool pushingLow = raw < outMin_ && error < 0.0;
    if (!pushingHigh && !pushingLow) integral_ += error * dt;

    lastOut_ = out;
    lastDt_ = dt;
    return out;
}

void PidController::applyActuatorFeedback(double appliedOutput) {
    // Back-calculation anti-windup (Astrom & Hagglund): bleed the integral
    // towards what the actuator really applied. For a smooth model the applied
    // value equals the request and nothing changes; for a staged model the
    // integral stops running away while the next stage is not available.
    if (gains_.ki <= 0.0 || gains_.trackingTimeS <= 0.0 || lastDt_ <= 0.0) return;
    integral_ += (appliedOutput - lastOut_) / (gains_.ki * gains_.trackingTimeS) * lastDt_;
}

void PidController::reset() {
    integral_ = 0.0;
    prevMeasured_ = 0.0;
    first_ = true;
    lastOut_ = lastDt_ = 0.0;
    lastP_ = lastI_ = lastD_ = 0.0;
}

}  // namespace chiller
