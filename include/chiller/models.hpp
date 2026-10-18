#pragma once
// The three simulated chiller models. Each one differs in how it turns a
// capacity request into compressor action, which extra sensors it has and
// which compressor protection it needs.

#include "chiller/simulated_chiller.hpp"

namespace chiller {

// Screw compressor: a slide valve gives smooth capacity down to a minimum.
// Protection: low oil pressure.
class ScrewChiller final : public SimulatedChiller {
public:
    ScrewChiller(ChillerConfig cfg, std::uint32_t seed = 42);
    std::string type() const override { return "screw"; }

protected:
    double translateRequest(double requestPct) override;
    void stepModelSpecific(double dtS) override;
    void addModelSensors(SensorData& reading) override;
    std::optional<DetectedFault> checkModelFaults(const SensorData& reading) const override;
    void resetModelSpecific() override;
    void repairModelSpecific() override;

private:
    double oilNominalKpa_, oilFailedKpa_, oilMinKpa_, oilTauS_;
    double oilPressureKpa_ = 0.0;
};

// Centrifugal compressor: inlet guide vanes give smooth capacity, but low
// load makes the compressor surge, so the minimum capacity is higher.
// Protection: surge (surge margin too small).
class CentrifugalChiller final : public SimulatedChiller {
public:
    CentrifugalChiller(ChillerConfig cfg, std::uint32_t seed = 42);
    std::string type() const override { return "centrifugal"; }

protected:
    double translateRequest(double requestPct) override;
    void stepModelSpecific(double dtS) override;
    void addModelSensors(SensorData& reading) override;
    std::optional<DetectedFault> checkModelFaults(const SensorData& reading) const override;
    void resetModelSpecific() override;
    void repairModelSpecific() override;

private:
    double normalSurgeMargin() const;
    double marginBasePct_, marginPerPct_, marginFailedPct_, marginMinPct_, marginTauS_;
    double surgeMarginPct_ = 0.0;
};

// Scroll compressors: several fixed-speed compressors switched on and off in
// stages, so capacity comes in steps (25 % each with 4 compressors).
// Staging uses a deadband and a minimum time between switches to avoid
// short-cycling. Protection: high discharge temperature.
class ScrollChiller final : public SimulatedChiller {
public:
    ScrollChiller(ChillerConfig cfg, std::uint32_t seed = 42);
    std::string type() const override { return "scroll"; }
    int activeStages() const { return activeStages_; }
    int stageCount() const { return stages_; }

protected:
    double translateRequest(double requestPct) override;
    void stepModelSpecific(double dtS) override;
    void addModelSensors(SensorData& reading) override;
    std::optional<DetectedFault> checkModelFaults(const SensorData& reading) const override;
    void resetModelSpecific() override;
    void repairModelSpecific() override;
    void onTrip() override;

private:
    double normalDischargeTemp() const;
    int stages_;
    double minSwitchS_, upMarginPct_, downMarginPct_;
    double dischargeBaseC_, dischargePerPct_, dischargeFailedC_, dischargeMaxC_, dischargeTauS_;
    int activeStages_ = 0;
    double lastSwitchS_ = -1e9;
    double dischargeTempC_ = 0.0;
};

}  // namespace chiller
