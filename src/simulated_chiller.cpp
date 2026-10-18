#include "chiller/simulated_chiller.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace chiller {

namespace {
constexpr double kWaterCpKjPerKgC = 4.186;
constexpr double kPi = 3.14159265358979323846;
constexpr double kSensorOpenCircuitC = -40.0;  // what a broken thermistor wire reads
constexpr double kFailedFlowFraction = 0.1;    // flow left after a pump failure

std::string fmt(double v, int precision = 1) {
    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(precision);
    os << v;
    return os.str();
}
}  // namespace

SimulatedChiller::SimulatedChiller(ChillerConfig cfg, std::uint32_t seed)
    : cfg_(std::move(cfg)), seed_(seed), rng_(seed) {
    supplyTempC_ = cfg_.water.initialSupplyTempC;
    condPressureKpa_ = cfg_.condenser.basePressureKpa;
}

double SimulatedChiller::approach(double value, double target, double dtS, double timeConstantS) {
    const double alpha = std::min(1.0, dtS / std::max(timeConstantS, 1e-9));
    return value + (target - value) * alpha;
}

double SimulatedChiller::gaussianNoise(double sigma) {
    if (sigma <= 0.0) return 0.0;
    // Box-Muller on the raw mt19937 output. Unlike std::normal_distribution this
    // gives the same numbers with GCC, Clang and MSVC, so results are reproducible.
    const double u1 = (static_cast<double>(rng_()) + 1.0) / 4294967296.0;  // (0, 1]
    const double u2 = static_cast<double>(rng_()) / 4294967296.0;           // [0, 1)
    return sigma * std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * kPi * u2);
}

SensorData SimulatedChiller::readSensors() {
    SensorData s;
    const double flowKgS = std::max(0.05, flowFraction_) * cfg_.water.nominalFlowKgS;
    const double trueReturn = supplyTempC_ + lastLoadKw_ / (flowKgS * kWaterCpKjPerKgC);

    s.chwSupplyTempC = isInjected(InjectedFault::SupplySensorFailure)
                           ? kSensorOpenCircuitC
                           : supplyTempC_ + gaussianNoise(cfg_.noise.temperatureC);
    s.chwReturnTempC = trueReturn + gaussianNoise(cfg_.noise.temperatureC);
    s.chwFlowPct = flowFraction_ * 100.0;
    s.condenserPressureKpa = condPressureKpa_ + gaussianNoise(cfg_.noise.pressureKpa);
    s.capacityPct = actualPct_;
    s.powerKw = actualPct_ / 100.0 * cfg_.ratedCapacityKw / cfg_.cop;
    addModelSensors(s);
    return s;
}

void SimulatedChiller::setCapacity(double requestPct) {
    if (!std::isfinite(requestPct)) throw std::invalid_argument("capacity request is not a number");
    const double request = std::clamp(requestPct, 0.0, 100.0);
    commandedPct_ = (state_ == ChillerState::Tripped) ? 0.0 : std::clamp(translateRequest(request), 0.0, 100.0);
}

double SimulatedChiller::applyMinimumCapacity(double requestPct, double minPct) {
    const bool currentlyOff = cycledOff_ || commandedPct_ <= 0.0;
    if (currentlyOff) {
        if (requestPct > 0.0 && requestPct >= minPct) {
            cycledOff_ = false;
            countStart();
            return std::clamp(requestPct, minPct, 100.0);
        }
        cycledOff_ = true;
        return 0.0;
    }
    if (requestPct < 0.5 * minPct) {
        cycledOff_ = true;
        return 0.0;
    }
    return std::clamp(requestPct, minPct, 100.0);
}

void SimulatedChiller::step(double dtS, double loadKw) {
    if (dtS <= 0.0) throw std::invalid_argument("dt must be > 0");

    // 1. Compressor capacity follows the command with a lag.
    const double healthyTarget = (state_ == ChillerState::Tripped) ? 0.0 : commandedPct_;
    double target = healthyTarget;
    if (state_ == ChillerState::Running && isInjected(InjectedFault::CapacityStuckHigh)) target = 100.0;
    actualPct_ = approach(actualPct_, target, dtS, cfg_.capacity.responseTimeS);
    expectedPct_ = approach(expectedPct_, healthyTarget, dtS, cfg_.capacity.responseTimeS);
    if (std::abs(actualPct_ - expectedPct_) > cfg_.limits.capacityResidualMaxPct) residualTimerS_ += dtS;
    else residualTimerS_ = 0.0;

    // 2. Chilled-water flow (collapses if the pump fails).
    const double flowTarget = isInjected(InjectedFault::PumpFailure) ? kFailedFlowFraction : 1.0;
    flowFraction_ = approach(flowFraction_, flowTarget, dtS, cfg_.water.flowTimeConstantS);

    // 3. Energy balance of the water loop. The building can only heat the water
    //    up to its own air temperature, so the load fades out near that limit
    //    (full load in the normal operating range below building_temp - 15 C).
    const double ceilingC = cfg_.water.buildingTempC;
    const double kneeC = ceilingC - 15.0;
    const double loadFactor =
        supplyTempC_ <= kneeC ? 1.0 : std::clamp((ceilingC - supplyTempC_) / (ceilingC - kneeC), 0.0, 1.0);
    const double effectiveLoadKw = loadKw * loadFactor;
    const double coolingKw = actualPct_ / 100.0 * cfg_.ratedCapacityKw * flowFraction_;
    supplyTempC_ += (effectiveLoadKw - coolingKw) * dtS / cfg_.water.thermalMassKjPerC;
    lastLoadKw_ = effectiveLoadKw;

    // 4. Condenser pressure rises with load, and a lot more if the fan fails.
    double condTarget = cfg_.condenser.basePressureKpa + cfg_.condenser.pressurePerCapacityPct * actualPct_;
    if (isInjected(InjectedFault::CondenserFanFailure)) condTarget += cfg_.condenser.fanFailureRiseKpa;
    condPressureKpa_ = approach(condPressureKpa_, condTarget, dtS, cfg_.condenser.timeConstantS);

    timeS_ += dtS;
    stepModelSpecific(dtS);
}

DetectedFault SimulatedChiller::makeFault(FaultCode code, const std::string& detail) const {
    return DetectedFault{code, cfg_.nativeCode(code), describe(code) + ": " + detail};
}

std::vector<DetectedFault> SimulatedChiller::checkFaults(const SensorData& s) const {
    std::vector<DetectedFault> faults;
    const Limits& lim = cfg_.limits;

    const bool sensorValid = std::isfinite(s.chwSupplyTempC) && s.chwSupplyTempC >= lim.supplyTempValidMinC &&
                             s.chwSupplyTempC <= lim.supplyTempValidMaxC;
    if (!sensorValid) {
        faults.push_back(makeFault(FaultCode::SensorFailure,
                                   "reading " + fmt(s.chwSupplyTempC) + " C is outside [" +
                                       fmt(lim.supplyTempValidMinC) + ", " + fmt(lim.supplyTempValidMaxC) + "]"));
    }
    if (s.condenserPressureKpa > lim.condenserPressureMaxKpa) {
        faults.push_back(makeFault(FaultCode::HighCondenserPressure,
                                   fmt(s.condenserPressureKpa, 0) + " kPa > " + fmt(lim.condenserPressureMaxKpa, 0)));
    }
    if (s.chwFlowPct < lim.minFlowPct) {
        faults.push_back(makeFault(FaultCode::LowChilledWaterFlow,
                                   fmt(s.chwFlowPct) + " % < " + fmt(lim.minFlowPct) + " %"));
    }
    if (sensorValid && s.chwSupplyTempC < lim.freezeProtectC) {
        faults.push_back(makeFault(FaultCode::FreezeRisk,
                                   fmt(s.chwSupplyTempC, 2) + " C < " + fmt(lim.freezeProtectC) + " C"));
    }
    if (residualTimerS_ >= cfg_.limits.capacityResidualTimeS) {
        faults.push_back(makeFault(FaultCode::CapacityControlFault,
                                   "actual " + fmt(s.capacityPct) + " % vs expected " + fmt(expectedPct_) + " % for " +
                                       fmt(residualTimerS_, 0) + " s"));
    }
    if (auto modelFault = checkModelFaults(s)) faults.push_back(*modelFault);
    return faults;
}

void SimulatedChiller::trip() {
    state_ = ChillerState::Tripped;
    commandedPct_ = 0.0;
    cycledOff_ = true;
    onTrip();
}

void SimulatedChiller::injectFault(InjectedFault fault) {
    if (fault != InjectedFault::None) injected_.insert(fault);
}

void SimulatedChiller::clearFaults() {
    injected_.clear();
    // The technician fixed the hardware: failed parts are back to normal.
    flowFraction_ = 1.0;
    condPressureKpa_ = cfg_.condenser.basePressureKpa + cfg_.condenser.pressurePerCapacityPct * actualPct_;
    expectedPct_ = actualPct_;
    residualTimerS_ = 0.0;
    repairModelSpecific();
    state_ = ChillerState::Running;
}

void SimulatedChiller::reset() {
    rng_.seed(seed_);
    timeS_ = 0.0;
    supplyTempC_ = cfg_.water.initialSupplyTempC;
    actualPct_ = 0.0;
    expectedPct_ = 0.0;
    residualTimerS_ = 0.0;
    commandedPct_ = 0.0;
    flowFraction_ = 1.0;
    condPressureKpa_ = cfg_.condenser.basePressureKpa;
    lastLoadKw_ = 0.0;
    cycledOff_ = false;
    starts_ = 0;
    state_ = ChillerState::Running;
    injected_.clear();
    resetModelSpecific();
}

}  // namespace chiller
