#include "chiller/types.hpp"

namespace chiller {

std::string toString(FaultCode code) {
    switch (code) {
        case FaultCode::None: return "";
        case FaultCode::SensorFailure: return "F01";
        case FaultCode::HighCondenserPressure: return "F02";
        case FaultCode::LowChilledWaterFlow: return "F03";
        case FaultCode::FreezeRisk: return "F04";
        case FaultCode::CompressorProtection: return "F05";
        case FaultCode::CapacityControlFault: return "F06";
    }
    return "";
}

std::string describe(FaultCode code) {
    switch (code) {
        case FaultCode::None: return "No fault";
        case FaultCode::SensorFailure: return "Supply temperature sensor failure";
        case FaultCode::HighCondenserPressure: return "High condenser pressure";
        case FaultCode::LowChilledWaterFlow: return "Low chilled-water flow";
        case FaultCode::FreezeRisk: return "Freeze risk: supply water too cold";
        case FaultCode::CompressorProtection: return "Compressor protection";
        case FaultCode::CapacityControlFault: return "Capacity control fault";
    }
    return "Unknown fault";
}

std::string configKey(FaultCode code) {
    switch (code) {
        case FaultCode::None: return "";
        case FaultCode::SensorFailure: return "sensor_failure";
        case FaultCode::HighCondenserPressure: return "high_condenser_pressure";
        case FaultCode::LowChilledWaterFlow: return "low_chilled_water_flow";
        case FaultCode::FreezeRisk: return "freeze_risk";
        case FaultCode::CompressorProtection: return "compressor_protection";
        case FaultCode::CapacityControlFault: return "capacity_control_fault";
    }
    return "";
}

std::string toString(InjectedFault fault) {
    switch (fault) {
        case InjectedFault::None: return "none";
        case InjectedFault::SupplySensorFailure: return "sensor_failure";
        case InjectedFault::CondenserFanFailure: return "condenser_fan_failure";
        case InjectedFault::PumpFailure: return "pump_failure";
        case InjectedFault::CapacityStuckHigh: return "capacity_stuck_high";
        case InjectedFault::CompressorProtection: return "compressor_protection";
    }
    return "none";
}

std::optional<InjectedFault> injectedFaultFromString(const std::string& name) {
    for (InjectedFault f : allInjectableFaults()) {
        if (toString(f) == name) return f;
    }
    if (name == "none") return InjectedFault::None;
    return std::nullopt;
}

FaultCode expectedCode(InjectedFault fault) {
    switch (fault) {
        case InjectedFault::None: return FaultCode::None;
        case InjectedFault::SupplySensorFailure: return FaultCode::SensorFailure;
        case InjectedFault::CondenserFanFailure: return FaultCode::HighCondenserPressure;
        case InjectedFault::PumpFailure: return FaultCode::LowChilledWaterFlow;
        case InjectedFault::CapacityStuckHigh: return FaultCode::CapacityControlFault;
        case InjectedFault::CompressorProtection: return FaultCode::CompressorProtection;
    }
    return FaultCode::None;
}

std::string toString(ChillerState state) {
    return state == ChillerState::Running ? "RUNNING" : "TRIPPED";
}

const std::vector<InjectedFault>& allInjectableFaults() {
    static const std::vector<InjectedFault> faults = {
        InjectedFault::SupplySensorFailure, InjectedFault::CondenserFanFailure,
        InjectedFault::PumpFailure, InjectedFault::CapacityStuckHigh,
        InjectedFault::CompressorProtection};
    return faults;
}

}  // namespace chiller
