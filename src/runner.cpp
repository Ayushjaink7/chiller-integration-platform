#include "chiller/runner.hpp"

#include <cmath>

#include "chiller/control_app.hpp"
#include "chiller/model_factory.hpp"

namespace chiller {

RunResult runScenario(ChillerModel& model, const Scenario& sc, const RunOptions& opt) {
    RunResult result;
    model.reset();
    ControlApp app(model, sc.pid, sc.setpointC);

    if (opt.logger) {
        result.runId = opt.logger->beginRun(model.config().modelId, model.type(), sc.name, opt.notes);
    }

    const int steps = static_cast<int>(std::llround(sc.durationS / sc.dtS));
    result.records.reserve(static_cast<std::size_t>(steps));
    bool injected = false;
    for (int i = 0; i < steps; ++i) {
        const double t = model.simTimeS();
        if (opt.stopAfterS && t > *opt.stopAfterS) break;
        if (opt.injection && !injected && t >= opt.injection->atS) {
            model.injectFault(opt.injection->fault);
            injected = true;
        }
        TickResult tick = app.tick(sc.loadPctAt(t), sc.dtS);
        for (const DetectedFault& f : tick.newFaults) {
            result.faults.push_back({t, f});
            if (opt.logger) opt.logger->logFaultEvent(t, model.config().modelId, f, "RAISED");
        }
        if (opt.logger) opt.logger->write(tick.record);
        result.records.push_back(std::move(tick.record));
    }
    if (opt.logger) opt.logger->flush();
    result.metrics = computeMetrics(result.records, sc, model.compressorStarts());
    return result;
}

FaultTrial runFaultTrial(const ChillerConfig& cfg, const Scenario& sc, InjectedFault fault, BlackBoxLogger* logger) {
    auto model = makeChiller(cfg, sc.seed);
    FaultTrial trial;
    trial.modelId = cfg.modelId;
    trial.modelType = model->type();
    trial.injected = fault;
    trial.expected = expectedCode(fault);
    trial.nativeCode = cfg.nativeCode(trial.expected);

    RunOptions opt;
    opt.logger = logger;
    opt.injection = Injection{sc.injectAtS, fault};
    opt.stopAfterS = sc.injectAtS + sc.detectionWindowS;
    opt.notes = "fault trial: " + toString(fault);
    const RunResult run = runScenario(*model, sc, opt);

    for (const TimedFault& tf : run.faults) {
        if (tf.timeS < sc.injectAtS) continue;  // raised before the injection: not caused by it
        if (trial.firstCode == FaultCode::None) trial.firstCode = tf.fault.code;
        if (tf.fault.code == trial.expected && !trial.detected) {
            trial.detected = true;
            trial.latencyS = tf.timeS - sc.injectAtS;
        }
    }
    for (const auto& r : run.records) {
        if (r.state == toString(ChillerState::Tripped)) trial.tripped = true;
    }
    return trial;
}

int countFalseAlarms(const ChillerConfig& cfg, const Scenario& sc, BlackBoxLogger* logger) {
    auto model = makeChiller(cfg, sc.seed);
    RunOptions opt;
    opt.logger = logger;
    opt.notes = "baseline: no injected faults";
    return static_cast<int>(runScenario(*model, sc, opt).faults.size());
}

}  // namespace chiller
