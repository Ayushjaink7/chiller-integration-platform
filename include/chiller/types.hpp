#pragma once
// Shared data types for every chiller model.
//
// These types are the "standard language" of the platform: every model,
// whatever its compressor technology, reports SensorData, raises standard
// FaultCodes and is logged as the same BlackBoxRecord.

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace chiller {

// Readings every model must provide, plus a map for model-specific sensors
// (oil pressure on a screw chiller, surge margin on a centrifugal, ...).
struct SensorData {
    double chwSupplyTempC = 0.0;      // chilled-water supply (leaving) temperature
    double chwReturnTempC = 0.0;      // chilled-water return (entering) temperature
    double chwFlowPct = 0.0;          // flow as % of nominal
    double condenserPressureKpa = 0.0;
    double powerKw = 0.0;             // compressor electrical power
    double capacityPct = 0.0;         // actual delivered capacity, % of rated
    std::map<std::string, double> extra;  // model-specific sensors
};

// Platform-wide fault codes. Each model maps its own native code onto one of these.
enum class FaultCode {
    None = 0,
    SensorFailure,          // F01: supply temperature sensor out of valid range
    HighCondenserPressure,  // F02
    LowChilledWaterFlow,    // F03
    FreezeRisk,             // F04: supply water close to freezing
    CompressorProtection,   // F05: model-specific compressor protection
    CapacityControlFault    // F06: actual capacity does not follow the command
};

// Physical problems we can inject into the simulation to test detection.
enum class InjectedFault {
    None = 0,
    SupplySensorFailure,     // sensor wire breaks, reads -40 C
    CondenserFanFailure,     // condenser pressure climbs
    PumpFailure,             // chilled-water flow collapses
    CapacityStuckHigh,       // capacity actuator stuck at 100% (slide valve / vanes / contactor)
    CompressorProtection     // model-specific: oil loss / surge / hot discharge
};

enum class ChillerState { Running, Tripped };

struct DetectedFault {
    FaultCode code = FaultCode::None;
    std::string nativeCode;   // the model's own code from its JSON config
    std::string description;
};

// The standardized black-box record. Same columns for every model;
// model-specific readings go into extrasJson.
struct BlackBoxRecord {
    double timeS = 0.0;
    std::string modelId;
    std::string modelType;
    double setpointC = 0.0;
    double loadPct = 0.0;
    double chwSupplyC = 0.0;
    double chwReturnC = 0.0;
    double flowPct = 0.0;
    double condPressureKpa = 0.0;
    double powerKw = 0.0;
    double capacityRequestPct = 0.0;  // what the PID asked for
    double capacityCommandPct = 0.0;  // what the model turned it into
    double capacityActualPct = 0.0;   // what the compressor actually delivers
    std::string state;                // "RUNNING" / "TRIPPED"
    std::string faultCode;            // standard code, e.g. "F02", empty if none
    std::string nativeFaultCode;      // model's own code, e.g. "SCR-E12"
    std::string extrasJson;           // model-specific sensors as a JSON object
};

// Helpers to convert enums to and from text.
std::string toString(FaultCode code);          // "F01" ... "F05", "" for None
std::string describe(FaultCode code);          // human-readable name
std::string configKey(FaultCode code);         // key used in the JSON "fault_codes" map
std::string toString(InjectedFault fault);     // "sensor_failure", ...
std::optional<InjectedFault> injectedFaultFromString(const std::string& name);
FaultCode expectedCode(InjectedFault fault);   // which standard code an injection should raise
std::string toString(ChillerState state);      // "RUNNING" / "TRIPPED"
const std::vector<InjectedFault>& allInjectableFaults();

}  // namespace chiller
