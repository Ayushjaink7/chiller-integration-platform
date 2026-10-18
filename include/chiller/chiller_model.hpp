#pragma once
// ChillerModel: the hardware abstraction layer (HAL).
//
// The control application only ever talks to this interface. Every chiller
// model (screw, centrifugal, scroll, ...) implements it in its own way, so the
// same control code runs unchanged on all of them. Adding a new model means
// writing one class and one JSON file; the control app does not change.

#include <cstdint>
#include <string>
#include <vector>

#include "chiller/config.hpp"
#include "chiller/types.hpp"

namespace chiller {

class ChillerModel {
public:
    virtual ~ChillerModel() = default;
    ChillerModel() = default;
    ChillerModel(const ChillerModel&) = delete;
    ChillerModel& operator=(const ChillerModel&) = delete;

    virtual const ChillerConfig& config() const = 0;
    virtual std::string type() const = 0;

    // Measured values (with sensor noise and any injected sensor fault).
    virtual SensorData readSensors() = 0;

    // Capacity request from the controller, 0..100 %. Each model decides how
    // to deliver it: slide valve, guide vanes or compressor stages.
    virtual void setCapacity(double requestPct) = 0;
    virtual double commandedCapacityPct() const = 0;

    // Advance the simulated physics by dtS seconds with the given building load.
    virtual void step(double dtS, double loadKw) = 0;

    // Compare a sensor reading with this model's limits.
    virtual std::vector<DetectedFault> checkFaults(const SensorData& reading) const = 0;

    // Safety: a tripped chiller delivers no capacity until clearFaults().
    virtual void trip() = 0;
    virtual ChillerState state() const = 0;

    // Fault injection for testing the detection logic.
    virtual void injectFault(InjectedFault fault) = 0;
    // "Repair and reset": removes injected faults, restores the failed parts
    // and releases the trip.
    virtual void clearFaults() = 0;

    virtual int compressorStarts() const = 0;
    virtual double simTimeS() const = 0;
    virtual void reset() = 0;  // back to initial conditions
};

}  // namespace chiller
