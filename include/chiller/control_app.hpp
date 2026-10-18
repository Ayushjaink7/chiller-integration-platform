#pragma once
// ControlApp: the one control application that runs unchanged on every model.
//
// It only uses the ChillerModel interface. Each tick:
//   1. read sensors           (model->readSensors)
//   2. check faults, trip     (model->checkFaults, model->trip)
//   3. PID -> capacity %      (PidController)
//   4. send the request       (model->setCapacity; the model decides how)
//   5. build a standard black-box record
//   6. advance the simulation (model->step)

#include <vector>

#include "chiller/chiller_model.hpp"
#include "chiller/pid_controller.hpp"

namespace chiller {

struct TickResult {
    BlackBoxRecord record;
    std::vector<DetectedFault> newFaults;  // faults raised for the first time this tick
};

class ControlApp {
public:
    ControlApp(ChillerModel& model, PidGains gains, double setpointC);

    TickResult tick(double loadPct, double dtS);

    void setSetpoint(double setpointC) { setpointC_ = setpointC; }
    double setpoint() const { return setpointC_; }

    // Operator reset after a trip: repair, clear alarms, restart control.
    void resetAlarms();
    // Back to initial conditions (new run).
    void restart();

    const std::vector<DetectedFault>& activeFaults() const { return active_; }
    ChillerModel& model() { return *model_; }
    const ChillerModel& model() const { return *model_; }
    PidController& pid() { return pid_; }

private:
    ChillerModel* model_;
    PidController pid_;
    double setpointC_;
    std::vector<DetectedFault> active_;  // latched until resetAlarms()
};

}  // namespace chiller
