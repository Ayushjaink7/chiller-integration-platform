#include "chiller/blackbox_logger.hpp"

#include <filesystem>
#include <mutex>
#include <stdexcept>

#include <sqlite3.h>

namespace chiller {

namespace {

const char* kSchema = R"SQL(
PRAGMA foreign_keys = ON;
CREATE TABLE IF NOT EXISTS runs (
    run_id      INTEGER PRIMARY KEY AUTOINCREMENT,
    started_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now')),
    model_id    TEXT NOT NULL,
    model_type  TEXT NOT NULL,
    scenario    TEXT NOT NULL,
    notes       TEXT NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS blackbox (
    id                    INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id                INTEGER NOT NULL REFERENCES runs(run_id),
    t_s                   REAL NOT NULL,
    model_id              TEXT NOT NULL,
    model_type            TEXT NOT NULL,
    setpoint_c            REAL,
    load_pct              REAL,
    chw_supply_c          REAL,
    chw_return_c          REAL,
    flow_pct              REAL,
    cond_pressure_kpa     REAL,
    power_kw              REAL,
    capacity_request_pct  REAL,
    capacity_command_pct  REAL,
    capacity_actual_pct   REAL,
    state                 TEXT,
    fault_code            TEXT,
    native_fault_code     TEXT,
    extras_json           TEXT
);
CREATE INDEX IF NOT EXISTS idx_blackbox_run_time ON blackbox(run_id, t_s);
CREATE TABLE IF NOT EXISTS fault_events (
    id                 INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id             INTEGER NOT NULL REFERENCES runs(run_id),
    t_s                REAL NOT NULL,
    model_id           TEXT NOT NULL,
    fault_code         TEXT NOT NULL,
    native_fault_code  TEXT NOT NULL,
    description        TEXT NOT NULL,
    event              TEXT NOT NULL,
    wall_clock         TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
);
CREATE INDEX IF NOT EXISTS idx_fault_events_run ON fault_events(run_id, t_s);
)SQL";

struct StmtDeleter {
    void operator()(sqlite3_stmt* s) const { sqlite3_finalize(s); }
};
using Stmt = std::unique_ptr<sqlite3_stmt, StmtDeleter>;

std::string text(sqlite3_stmt* s, int col) {
    const unsigned char* p = sqlite3_column_text(s, col);
    return p ? reinterpret_cast<const char*>(p) : "";
}

}  // namespace

struct BlackBoxLogger::Impl {
    sqlite3* db = nullptr;
    mutable std::mutex mu;
    int batchSize = 200;
    int pending = 0;
    bool inTransaction = false;
    std::int64_t runId = -1;
    Stmt insertRecord;
    Stmt insertFault;

    void exec(const char* sql) {
        char* err = nullptr;
        if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            std::string msg = err ? err : "unknown error";
            sqlite3_free(err);
            throw std::runtime_error("SQLite: " + msg);
        }
    }

    Stmt prepare(const char* sql) const {
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string("SQLite prepare failed: ") + sqlite3_errmsg(db));
        }
        return Stmt(s);
    }

    void step(sqlite3_stmt* s) {
        const int rc = sqlite3_step(s);
        sqlite3_reset(s);
        sqlite3_clear_bindings(s);
        if (rc != SQLITE_DONE) throw std::runtime_error(std::string("SQLite write failed: ") + sqlite3_errmsg(db));
    }

    void begin() {
        if (!inTransaction) {
            exec("BEGIN");
            inTransaction = true;
        }
    }

    void commit() {
        if (inTransaction) {
            exec("COMMIT");
            inTransaction = false;
            pending = 0;
        }
    }
};

BlackBoxLogger::BlackBoxLogger(const std::string& path, int batchSize) : impl_(std::make_unique<Impl>()) {
    impl_->batchSize = batchSize > 0 ? batchSize : 1;
    if (path != ":memory:") {
        const auto parent = std::filesystem::path(path).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
    }
    if (sqlite3_open(path.c_str(), &impl_->db) != SQLITE_OK) {
        std::string msg = impl_->db ? sqlite3_errmsg(impl_->db) : "out of memory";
        sqlite3_close(impl_->db);
        throw std::runtime_error("cannot open database " + path + ": " + msg);
    }
    sqlite3_busy_timeout(impl_->db, 2000);
    if (path != ":memory:") impl_->exec("PRAGMA journal_mode = WAL; PRAGMA synchronous = NORMAL;");
    impl_->exec(kSchema);
    impl_->insertRecord = impl_->prepare(
        "INSERT INTO blackbox (run_id, t_s, model_id, model_type, setpoint_c, load_pct, chw_supply_c, "
        "chw_return_c, flow_pct, cond_pressure_kpa, power_kw, capacity_request_pct, capacity_command_pct, "
        "capacity_actual_pct, state, fault_code, native_fault_code, extras_json) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    impl_->insertFault = impl_->prepare(
        "INSERT INTO fault_events (run_id, t_s, model_id, fault_code, native_fault_code, description, event) "
        "VALUES (?,?,?,?,?,?,?)");
}

BlackBoxLogger::~BlackBoxLogger() {
    try {
        std::lock_guard<std::mutex> lock(impl_->mu);
        impl_->commit();
    } catch (...) {
        // Never throw from a destructor.
    }
    impl_->insertRecord.reset();
    impl_->insertFault.reset();
    sqlite3_close(impl_->db);
}

std::int64_t BlackBoxLogger::beginRun(const std::string& modelId, const std::string& modelType,
                                      const std::string& scenario, const std::string& notes) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->commit();
    Stmt s = impl_->prepare("INSERT INTO runs (model_id, model_type, scenario, notes) VALUES (?,?,?,?)");
    sqlite3_bind_text(s.get(), 1, modelId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s.get(), 2, modelType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s.get(), 3, scenario.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s.get(), 4, notes.c_str(), -1, SQLITE_TRANSIENT);
    impl_->step(s.get());
    impl_->runId = sqlite3_last_insert_rowid(impl_->db);
    return impl_->runId;
}

std::int64_t BlackBoxLogger::currentRunId() const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->runId;
}

void BlackBoxLogger::write(const BlackBoxRecord& r) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    if (impl_->runId < 0) throw std::logic_error("BlackBoxLogger::write called before beginRun");
    impl_->begin();
    sqlite3_stmt* s = impl_->insertRecord.get();
    sqlite3_bind_int64(s, 1, impl_->runId);
    sqlite3_bind_double(s, 2, r.timeS);
    sqlite3_bind_text(s, 3, r.modelId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 4, r.modelType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(s, 5, r.setpointC);
    sqlite3_bind_double(s, 6, r.loadPct);
    sqlite3_bind_double(s, 7, r.chwSupplyC);
    sqlite3_bind_double(s, 8, r.chwReturnC);
    sqlite3_bind_double(s, 9, r.flowPct);
    sqlite3_bind_double(s, 10, r.condPressureKpa);
    sqlite3_bind_double(s, 11, r.powerKw);
    sqlite3_bind_double(s, 12, r.capacityRequestPct);
    sqlite3_bind_double(s, 13, r.capacityCommandPct);
    sqlite3_bind_double(s, 14, r.capacityActualPct);
    sqlite3_bind_text(s, 15, r.state.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 16, r.faultCode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 17, r.nativeFaultCode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 18, r.extrasJson.c_str(), -1, SQLITE_TRANSIENT);
    impl_->step(s);
    if (++impl_->pending >= impl_->batchSize) impl_->commit();
}

void BlackBoxLogger::logFaultEvent(double timeS, const std::string& modelId, const DetectedFault& f,
                                   const std::string& event) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    if (impl_->runId < 0) throw std::logic_error("BlackBoxLogger::logFaultEvent called before beginRun");
    impl_->begin();
    sqlite3_stmt* s = impl_->insertFault.get();
    const std::string code = toString(f.code);
    sqlite3_bind_int64(s, 1, impl_->runId);
    sqlite3_bind_double(s, 2, timeS);
    sqlite3_bind_text(s, 3, modelId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 4, code.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 5, f.nativeCode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 6, f.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 7, event.c_str(), -1, SQLITE_TRANSIENT);
    impl_->step(s);
    impl_->commit();  // fault events are important: make them durable right away
}

void BlackBoxLogger::flush() {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->commit();
}

std::vector<BlackBoxRecord> BlackBoxLogger::readRun(std::int64_t runId) const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    Stmt s = impl_->prepare(
        "SELECT t_s, model_id, model_type, setpoint_c, load_pct, chw_supply_c, chw_return_c, flow_pct, "
        "cond_pressure_kpa, power_kw, capacity_request_pct, capacity_command_pct, capacity_actual_pct, state, "
        "fault_code, native_fault_code, extras_json FROM blackbox WHERE run_id = ? ORDER BY t_s");
    sqlite3_bind_int64(s.get(), 1, runId);
    std::vector<BlackBoxRecord> out;
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        BlackBoxRecord r;
        r.timeS = sqlite3_column_double(s.get(), 0);
        r.modelId = text(s.get(), 1);
        r.modelType = text(s.get(), 2);
        r.setpointC = sqlite3_column_double(s.get(), 3);
        r.loadPct = sqlite3_column_double(s.get(), 4);
        r.chwSupplyC = sqlite3_column_double(s.get(), 5);
        r.chwReturnC = sqlite3_column_double(s.get(), 6);
        r.flowPct = sqlite3_column_double(s.get(), 7);
        r.condPressureKpa = sqlite3_column_double(s.get(), 8);
        r.powerKw = sqlite3_column_double(s.get(), 9);
        r.capacityRequestPct = sqlite3_column_double(s.get(), 10);
        r.capacityCommandPct = sqlite3_column_double(s.get(), 11);
        r.capacityActualPct = sqlite3_column_double(s.get(), 12);
        r.state = text(s.get(), 13);
        r.faultCode = text(s.get(), 14);
        r.nativeFaultCode = text(s.get(), 15);
        r.extrasJson = text(s.get(), 16);
        out.push_back(std::move(r));
    }
    return out;
}

std::int64_t BlackBoxLogger::countRecords(std::int64_t runId) const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    Stmt s = impl_->prepare("SELECT COUNT(*) FROM blackbox WHERE run_id = ?");
    sqlite3_bind_int64(s.get(), 1, runId);
    return sqlite3_step(s.get()) == SQLITE_ROW ? sqlite3_column_int64(s.get(), 0) : 0;
}

std::vector<FaultEvent> BlackBoxLogger::recentFaultEvents(int limit) const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    Stmt s = impl_->prepare(
        "SELECT id, run_id, t_s, model_id, fault_code, native_fault_code, description, event, wall_clock "
        "FROM fault_events ORDER BY id DESC LIMIT ?");
    sqlite3_bind_int(s.get(), 1, limit);
    std::vector<FaultEvent> out;
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        FaultEvent e;
        e.id = sqlite3_column_int64(s.get(), 0);
        e.runId = sqlite3_column_int64(s.get(), 1);
        e.timeS = sqlite3_column_double(s.get(), 2);
        e.modelId = text(s.get(), 3);
        e.faultCode = text(s.get(), 4);
        e.nativeCode = text(s.get(), 5);
        e.description = text(s.get(), 6);
        e.event = text(s.get(), 7);
        e.wallClock = text(s.get(), 8);
        out.push_back(std::move(e));
    }
    return out;
}

std::vector<RunInfo> BlackBoxLogger::recentRuns(int limit) const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    Stmt s = impl_->prepare(
        "SELECT r.run_id, r.started_at, r.model_id, r.model_type, r.scenario, r.notes, "
        "(SELECT COUNT(*) FROM blackbox b WHERE b.run_id = r.run_id) "
        "FROM runs r ORDER BY r.run_id DESC LIMIT ?");
    sqlite3_bind_int(s.get(), 1, limit);
    std::vector<RunInfo> out;
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        RunInfo r;
        r.runId = sqlite3_column_int64(s.get(), 0);
        r.startedAt = text(s.get(), 1);
        r.modelId = text(s.get(), 2);
        r.modelType = text(s.get(), 3);
        r.scenario = text(s.get(), 4);
        r.notes = text(s.get(), 5);
        r.records = sqlite3_column_int64(s.get(), 6);
        out.push_back(std::move(r));
    }
    return out;
}

}  // namespace chiller
