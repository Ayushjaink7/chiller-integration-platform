#include "chiller/control_app.hpp"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace chiller {

ControlApp::ControlApp(ChillerModel& model, PidGains gains, double setpointC)
    : model_(&model), pid_(gains, 0.0, 100.0), setpointC_(setpointC) {}

TickResult ControlApp::tick(double loadPct, double dtS) {
    TickResult result;
    ChillerModel& m = *model_;
    const double timeS = m.simTimeS();

    // 1-2. Read sensors and check every limit of this model.
    const SensorData s = m.readSensors();
    for (const DetectedFault& f : m.checkFaults(s)) {
        const bool known = std::any_of(active_.begin(), active_.end(),
                                       [&](const DetectedFault& a) { return a.code == f.code; });
        if (!known) {
            active_.push_back(f);
            result.newFaults.push_back(f);
        }
    }
    if (!result.newFaults.empty() && m.state() == ChillerState::Running) m.trip();

    // 3-4. PID only runs while the chiller is healthy.
    double request = 0.0;
    if (m.state() == ChillerState::Running) {
        request = pid_.update(setpointC_, s.chwSupplyTempC, dtS);
    } else {
        pid_.reset();  // restart cleanly after the operator resets the trip
    }
    m.setCapacity(request);
    if (m.state() == ChillerState::Running && pid_.gains().actuatorFeedback) {
        pid_.applyActuatorFeedback(m.commandedCapacityPct());
    }

    // 5. Standard black-box record (same fields for every model).
    BlackBoxRecord& r = result.record;
    r.timeS = timeS;
    r.modelId = m.config().modelId;
    r.modelType = m.type();
    r.setpointC = setpointC_;
    r.loadPct = loadPct;
    r.chwSupplyC = s.chwSupplyTempC;
    r.chwReturnC = s.chwReturnTempC;
    r.flowPct = s.chwFlowPct;
    r.condPressureKpa = s.condenserPressureKpa;
    r.powerKw = s.powerKw;
    r.capacityRequestPct = request;
    r.capacityCommandPct = m.commandedCapacityPct();
    r.capacityActualPct = s.capacityPct;
    r.state = toString(m.state());
    for (const DetectedFault& f : active_) {
        r.faultCode += (r.faultCode.empty() ? "" : ",") + toString(f.code);
        r.nativeFaultCode += (r.nativeFaultCode.empty() ? "" : ",") + f.nativeCode;
    }
    r.extrasJson = nlohmann::json(s.extra).dump();

    // 6. Advance the simulated plant.
    m.step(dtS, loadPct / 100.0 * m.config().ratedCapacityKw);
    return result;
}

void ControlApp::resetAlarms() {
    model_->clearFaults();
    active_.clear();
    pid_.reset();
}

void ControlApp::restart() {
    model_->reset();
    active_.clear();
    pid_.reset();
}

}  // namespace chiller
