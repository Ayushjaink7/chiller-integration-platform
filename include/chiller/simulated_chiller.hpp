#pragma once
// SimulatedChiller: shared simulation for all models.
//
// It implements the ChillerModel interface with physics that every chiller
// has in common (water loop, condenser, flow, common faults). Concrete models
// only fill in what is different through a few protected hooks:
//   translateRequest   - how a capacity request becomes a capacity command
//   stepModelSpecific  - physics of model-specific parts (oil, surge, discharge)
//   addModelSensors    - model-specific sensor readings
//   checkModelFaults   - model-specific compressor protection
//
// Physics (lumped, one time constant per part):
//   capacity   : first-order lag towards the command (response_time_s)
//   water loop : C * dT_supply/dt = load_kW - cooling_kW
//                (load fades out as the water nears building air temperature)
//   return temp: T_supply + load_kW / (flow * cp)
//   condenser  : first-order lag towards base + k * capacity (+ rise if fan failed)
//
// Capacity-control check (model-based fault detection): the base keeps an
// "expected capacity" by running the command through the same lag the healthy
// actuator has. If the measured capacity stays more than capacity_residual_max_pct
// away from it for capacity_residual_time_s, the actuator is not obeying (F06).

#include <cstdint>
#include <optional>
#include <random>
#include <set>

#include "chiller/chiller_model.hpp"

namespace chiller {

class SimulatedChiller : public ChillerModel {
public:
    SimulatedChiller(ChillerConfig cfg, std::uint32_t seed);

    const ChillerConfig& config() const override { return cfg_; }
    SensorData readSensors() override;
    void setCapacity(double requestPct) override;
    double commandedCapacityPct() const override { return commandedPct_; }
    void step(double dtS, double loadKw) override;
    std::vector<DetectedFault> checkFaults(const SensorData& reading) const override;
    void trip() override;
    ChillerState state() const override { return state_; }
    void injectFault(InjectedFault fault) override;
    void clearFaults() override;
    int compressorStarts() const override { return starts_; }
    double simTimeS() const override { return timeS_; }
    void reset() override;

    // True (noise-free) values, used by tests.
    double trueSupplyTempC() const { return supplyTempC_; }
    double actualCapacityPct() const { return actualPct_; }
    double expectedCapacityPct() const { return expectedPct_; }
    bool isInjected(InjectedFault fault) const { return injected_.count(fault) > 0; }

protected:
    virtual double translateRequest(double requestPct) = 0;
    virtual void stepModelSpecific(double dtS) = 0;
    virtual void addModelSensors(SensorData& reading) = 0;
    virtual std::optional<DetectedFault> checkModelFaults(const SensorData& reading) const = 0;
    virtual void resetModelSpecific() = 0;
    virtual void repairModelSpecific() = 0;
    virtual void onTrip() {}

    // Smooth-capacity helper (screw, centrifugal): never run below the minimum
    // capacity; switch off when the request falls below half of it and restart
    // only when it reaches the minimum again (hysteresis against chattering).
    double applyMinimumCapacity(double requestPct, double minPct);

    DetectedFault makeFault(FaultCode code, const std::string& detail) const;
    double gaussianNoise(double sigma);
    // Moves value towards target with a first-order lag.
    static double approach(double value, double target, double dtS, double timeConstantS);

    void countStart() { ++starts_; }
    double actualPct() const { return actualPct_; }

private:
    ChillerConfig cfg_;
    std::uint32_t seed_;
    std::mt19937 rng_;

    double timeS_ = 0.0;
    double supplyTempC_ = 0.0;
    double actualPct_ = 0.0;
    double expectedPct_ = 0.0;      // what a healthy actuator would deliver
    double residualTimerS_ = 0.0;   // how long |actual - expected| has been too large
    double commandedPct_ = 0.0;
    double flowFraction_ = 1.0;
    double condPressureKpa_ = 0.0;
    double lastLoadKw_ = 0.0;
    bool cycledOff_ = false;
    int starts_ = 0;
    ChillerState state_ = ChillerState::Running;
    std::set<InjectedFault> injected_;
};

}  // namespace chiller
