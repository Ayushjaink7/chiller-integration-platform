// chiller_server: REST API + live simulation for the React dashboard.
//
//   chiller_server [--port 8080] [--host 127.0.0.1] [--speed 5]
//                  [--config-dir config] [--db data/live.db] [--dashboard dashboard/dist]
//
// GET  /api/health                 liveness check
// GET  /api/models                 all configured models
// GET  /api/state                  latest reading, active faults, settings
// GET  /api/history?seconds=600    time series for the charts
// GET  /api/faults?limit=50        fault events from SQLite (newest first)
// GET  /api/runs?limit=20          runs stored in SQLite
// POST /api/select   {"model_id": "SCR-1000"}
// POST /api/setpoint {"value": 7.0}
// POST /api/load     {"value": 80}
// POST /api/speed    {"value": 10}
// POST /api/inject   {"fault": "pump_failure"}
// POST /api/reset                  repair + clear alarms after a trip
// POST /api/restart                restart the current model from initial conditions

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "live_simulation.hpp"

using nlohmann::json;
using chiller::LiveOptions;
using chiller::LiveSimulation;

namespace {

httplib::Server* g_server = nullptr;

void onSignal(int) {
    if (g_server) g_server->stop();
}

struct ServerOptions {
    LiveOptions live;
    std::string host = "127.0.0.1";
    int port = 8080;
    std::string dashboardDir = "dashboard/dist";
};

ServerOptions parseArgs(int argc, char** argv) {
    ServerOptions o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("missing value after " + a);
            return argv[++i];
        };
        if (a == "--port") o.port = std::stoi(next());
        else if (a == "--host") o.host = next();
        else if (a == "--speed") o.live.speed = std::stod(next());
        else if (a == "--config-dir") o.live.configDir = next();
        else if (a == "--scenario") o.live.scenarioPath = next();
        else if (a == "--db") o.live.dbPath = next();
        else if (a == "--dashboard") o.dashboardDir = next();
        else if (a == "-h" || a == "--help") {
            std::cout << "chiller_server [--port 8080] [--host 127.0.0.1] [--speed 5] [--config-dir config]\n"
                         "               [--db data/live.db] [--dashboard dashboard/dist]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("unknown option " + a);
        }
    }
    return o;
}

void sendJson(httplib::Response& res, const json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

// Parses the request body as JSON; on failure writes a 400 response and returns false.
bool parseBody(const httplib::Request& req, httplib::Response& res, json& out) {
    try {
        out = req.body.empty() ? json::object() : json::parse(req.body);
        if (!out.is_object()) throw std::runtime_error("body must be a JSON object");
        return true;
    } catch (const std::exception& e) {
        sendJson(res, {{"error", std::string("invalid JSON body: ") + e.what()}}, 400);
        return false;
    }
}

// Handles {"value": number} commands.
void numberCommand(LiveSimulation& sim, const httplib::Request& req, httplib::Response& res,
                   std::string (LiveSimulation::*setter)(double)) {
    json body;
    if (!parseBody(req, res, body)) return;
    if (!body.contains("value") || !body["value"].is_number()) {
        sendJson(res, {{"error", "expected {\"value\": <number>}"}}, 400);
        return;
    }
    const std::string err = (sim.*setter)(body["value"].get<double>());
    if (!err.empty()) return sendJson(res, {{"error", err}}, 400);
    sendJson(res, sim.state());
}

int intParam(const httplib::Request& req, const char* name, int fallback) {
    if (!req.has_param(name)) return fallback;
    try {
        return std::stoi(req.get_param_value(name));
    } catch (...) {
        return fallback;
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        ServerOptions opt = parseArgs(argc, argv);
        LiveSimulation sim(opt.live);
        httplib::Server svr;

        svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
            sendJson(res, {{"status", "ok"}});
        });
        svr.Get("/api/models", [&](const httplib::Request&, httplib::Response& res) { sendJson(res, sim.models()); });
        svr.Get("/api/state", [&](const httplib::Request&, httplib::Response& res) { sendJson(res, sim.state()); });
        svr.Get("/api/history", [&](const httplib::Request& req, httplib::Response& res) {
            sendJson(res, sim.history(intParam(req, "seconds", 600)));
        });
        svr.Get("/api/faults", [&](const httplib::Request& req, httplib::Response& res) {
            sendJson(res, sim.faults(intParam(req, "limit", 50)));
        });
        svr.Get("/api/runs", [&](const httplib::Request& req, httplib::Response& res) {
            sendJson(res, sim.runs(intParam(req, "limit", 20)));
        });

        svr.Post("/api/select", [&](const httplib::Request& req, httplib::Response& res) {
            json body;
            if (!parseBody(req, res, body)) return;
            const std::string id = body.value("model_id", "");
            const std::string err = sim.selectModel(id);
            if (!err.empty()) return sendJson(res, {{"error", err}}, 400);
            sendJson(res, sim.state());
        });
        svr.Post("/api/setpoint", [&](const httplib::Request& req, httplib::Response& res) {
            numberCommand(sim, req, res, &LiveSimulation::setSetpoint);
        });
        svr.Post("/api/load", [&](const httplib::Request& req, httplib::Response& res) {
            numberCommand(sim, req, res, &LiveSimulation::setLoad);
        });
        svr.Post("/api/speed", [&](const httplib::Request& req, httplib::Response& res) {
            numberCommand(sim, req, res, &LiveSimulation::setSpeed);
        });
        svr.Post("/api/inject", [&](const httplib::Request& req, httplib::Response& res) {
            json body;
            if (!parseBody(req, res, body)) return;
            const std::string err = sim.inject(body.value("fault", ""));
            if (!err.empty()) return sendJson(res, {{"error", err}}, 400);
            sendJson(res, sim.state());
        });
        svr.Post("/api/reset", [&](const httplib::Request&, httplib::Response& res) {
            sim.resetAlarms();
            sendJson(res, sim.state());
        });
        svr.Post("/api/restart", [&](const httplib::Request&, httplib::Response& res) {
            sim.restart();
            sendJson(res, sim.state());
        });

        const bool hasDashboard = std::filesystem::exists(std::filesystem::path(opt.dashboardDir) / "index.html");
        if (hasDashboard) {
            svr.set_mount_point("/", opt.dashboardDir);
        } else {
            svr.Get("/", [&](const httplib::Request&, httplib::Response& res) {
                res.set_content("Chiller API is running. Dashboard not found in '" + opt.dashboardDir +
                                    "' (build it with: cd dashboard && npm install && npm run build).",
                                "text/plain");
            });
        }

        g_server = &svr;
        std::signal(SIGINT, onSignal);
        std::signal(SIGTERM, onSignal);

        sim.start();
        std::cout << "Chiller platform server\n"
                  << "  API        : http://" << (opt.host == "0.0.0.0" ? "localhost" : opt.host) << ":" << opt.port
                  << "/api/state\n"
                  << "  Dashboard  : "
                  << (hasDashboard ? "http://localhost:" + std::to_string(opt.port) : std::string("not built"))
                  << "\n  Database   : " << opt.live.dbPath << "\n  Speed      : " << opt.live.speed
                  << "x real time\n  Press Ctrl+C to stop.\n"
                  << std::flush;

        if (!svr.listen(opt.host, opt.port)) {
            std::cerr << "error: could not listen on " << opt.host << ":" << opt.port
                      << " (is the port already in use? try --port 8081)\n";
            sim.stop();
            return 1;
        }
        sim.stop();
        std::cout << "Server stopped.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
