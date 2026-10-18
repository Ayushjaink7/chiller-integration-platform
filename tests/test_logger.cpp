#include <gtest/gtest.h>

#include <filesystem>

#include "chiller/blackbox_logger.hpp"
#include "chiller/model_factory.hpp"
#include "chiller/runner.hpp"
#include "test_helpers.hpp"

using namespace chiller;

namespace {
BlackBoxRecord sampleRecord(double t) {
    BlackBoxRecord r;
    r.timeS = t;
    r.modelId = "SCR-1000";
    r.modelType = "screw";
    r.setpointC = 7.0;
    r.loadPct = 50.0;
    r.chwSupplyC = 7.1;
    r.chwReturnC = 9.6;
    r.flowPct = 100.0;
    r.condPressureKpa = 900.0;
    r.powerKw = 89.3;
    r.capacityRequestPct = 51.0;
    r.capacityCommandPct = 51.0;
    r.capacityActualPct = 50.2;
    r.state = "RUNNING";
    r.extrasJson = R"({"oil_pressure_kpa":320.0})";
    return r;
}
}  // namespace

TEST(BlackBoxLogger, WritesAndReadsBackRecords) {
    BlackBoxLogger log(":memory:");
    const auto run = log.beginRun("SCR-1000", "screw", "unit-test");
    for (int i = 0; i < 10; ++i) log.write(sampleRecord(i));
    log.flush();
    ASSERT_EQ(log.countRecords(run), 10);
    const auto back = log.readRun(run);
    ASSERT_EQ(back.size(), 10u);
    EXPECT_DOUBLE_EQ(back[3].timeS, 3.0);
    EXPECT_EQ(back[3].modelType, "screw");
    EXPECT_DOUBLE_EQ(back[3].capacityActualPct, 50.2);
    EXPECT_EQ(back[3].extrasJson, R"({"oil_pressure_kpa":320.0})");
}

TEST(BlackBoxLogger, RunsAreKeptSeparate) {
    BlackBoxLogger log(":memory:", 3);
    const auto a = log.beginRun("SCR-1000", "screw", "a");
    for (int i = 0; i < 5; ++i) log.write(sampleRecord(i));
    const auto b = log.beginRun("SRL-400", "scroll", "b");
    for (int i = 0; i < 7; ++i) log.write(sampleRecord(i));
    EXPECT_NE(a, b);
    EXPECT_EQ(log.countRecords(a), 5);
    EXPECT_EQ(log.countRecords(b), 7);
    const auto runs = log.recentRuns(10);
    ASSERT_EQ(runs.size(), 2u);
    EXPECT_EQ(runs[0].runId, b);  // newest first
    EXPECT_EQ(runs[0].records, 7);
}

TEST(BlackBoxLogger, StoresFaultEventsNewestFirst) {
    BlackBoxLogger log(":memory:");
    log.beginRun("CEN-2000", "centrifugal", "faults");
    log.logFaultEvent(10.0, "CEN-2000", {FaultCode::LowChilledWaterFlow, "CEN-207", "Low flow"}, "RAISED");
    log.logFaultEvent(20.0, "CEN-2000", {FaultCode::CompressorProtection, "CEN-415", "Surge"}, "RAISED");
    const auto events = log.recentFaultEvents(10);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].faultCode, "F05");
    EXPECT_EQ(events[0].nativeCode, "CEN-415");
    EXPECT_EQ(events[1].faultCode, "F03");
    EXPECT_FALSE(events[0].wallClock.empty());
}

TEST(BlackBoxLogger, WriteBeforeBeginRunIsAnError) {
    BlackBoxLogger log(":memory:");
    EXPECT_THROW(log.write(sampleRecord(0)), std::logic_error);
}

TEST(BlackBoxLogger, FileDatabaseSurvivesReopen) {
    const auto path = std::filesystem::path(testing::TempDir()) / "chiller_test_db" / "bb.db";
    std::filesystem::remove_all(path.parent_path());
    std::int64_t run = 0;
    {
        BlackBoxLogger log(path.string());
        run = log.beginRun("SCR-1000", "screw", "persist");
        for (int i = 0; i < 25; ++i) log.write(sampleRecord(i));
    }  // destructor commits
    BlackBoxLogger reopened(path.string());
    EXPECT_EQ(reopened.countRecords(run), 25);
}

TEST(BlackBoxLogger, EveryModelUsesTheSameRecordFormat) {
    BlackBoxLogger log(":memory:");
    const Scenario sc = testutil::defaultScenario();
    for (const auto& type : testutil::modelTypes()) {
        auto model = makeChiller(testutil::modelConfig(type), sc.seed);
        RunOptions opt;
        opt.logger = &log;
        const auto result = runScenario(*model, sc, opt);
        const auto stored = log.readRun(result.runId);
        ASSERT_EQ(stored.size(), result.records.size()) << type;
        EXPECT_EQ(stored.front().modelType, type);
        EXPECT_NE(stored.back().extrasJson, "{}") << "model-specific sensors go in extras_json";
    }
}
