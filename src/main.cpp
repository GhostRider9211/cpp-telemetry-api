#include "httplib.h"
#include "telemetry.hpp"
#include "telemetry_alerting.hpp"
#include "telemetry_config.hpp"
#include "telemetry_diagnostics.hpp"
#include "telemetry_metrics.hpp"
#include "telemetry_monitoring.hpp"
#include "telemetry_pipeline.hpp"
#include "telemetry_serializer.hpp"
#include "telemetry_sinks.hpp"
#include "telemetry_store.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{

Telemetry parse_telemetry(const json& body)
{
    Telemetry t;
    t.sensor_id = body.at("sensor_id").get<std::string>();
    t.temperature = body.at("temperature").get<double>();
    t.humidity = body.at("humidity").get<double>();
    t.timestamp = body.at("timestamp").get<long>();
    return t;
}

telemetry::EventContext request_context(const httplib::Request& req)
{
    telemetry::EventContext context;
    if(req.has_header("X-Correlation-Id"))
        context.correlation_id = req.get_header_value("X-Correlation-Id");
    if(req.has_header("X-Request-Id"))
        context.request_id = req.get_header_value("X-Request-Id");
    context.metadata.emplace("remote_addr", req.remote_addr);
    context.metadata.emplace("method", req.method);
    context.metadata.emplace("path", req.path);
    return context;
}

} // namespace

int main()
{
    const char* config_path_env = std::getenv("TELEMETRY_CONFIG");
    const std::string config_path = config_path_env ? config_path_env : "";

    auto config = telemetry::ConfigLoader::from_file(config_path);
    telemetry::MetricRegistry registry;
    auto serializer = telemetry::make_json_serializer();
    telemetry::TelemetryPipeline pipeline(config.pipeline, serializer, registry);

    for(const auto& sink_config : config.sinks)
        pipeline.add_sink(telemetry::make_sink(sink_config), sink_config);

    pipeline.start();

    telemetry::EventLogger logger(pipeline);
    telemetry::Diagnostics diagnostics(registry);
    diagnostics.attach_logger(logger);
    diagnostics.install_terminate_handler();

    telemetry::AlertEngine alert_engine(registry);
    alert_engine.add_notifier(std::make_shared<telemetry::StdoutAlertNotifier>());
    for(const auto& rule : config.alerts)
        alert_engine.add_rule(rule);
    alert_engine.start();

    telemetry::ConfigManager config_manager(config_path, [&pipeline, &logger](const telemetry::TelemetryConfig& updated) {
        pipeline.update_runtime_config(updated.pipeline);
        logger.info("config.reload", "telemetry configuration reloaded");
    });
    if(!config_path.empty())
        config_manager.start();

    TelemetryStore store;
    httplib::Server svr;

    auto readings_total = registry.counter("sensor_readings_total", {"Accepted sensor readings"});
    auto temperature_summary = registry.summary("sensor_temperature_celsius", {"Observed sensor temperature", "celsius"});
    auto humidity_summary = registry.summary("sensor_humidity_percent", {"Observed sensor humidity", "percent"});
    auto request_latency = registry.summary("http_request_duration_seconds", {"HTTP request duration", "seconds"});
    auto active_requests = registry.gauge("http_active_requests", {"Active HTTP requests"});

    telemetry::register_monitoring_routes(
        svr, registry, pipeline, diagnostics, *serializer, config.monitoring.metrics_bearer_token);

    svr.Post("/telemetry", [&](const httplib::Request& req, httplib::Response& res) {
        active_requests->increment(1.0);
        telemetry::Timer timer(*request_latency);

        try
        {
            const auto body = json::parse(req.body);
            auto t = parse_telemetry(body);
            store.add(t);

            readings_total->increment();
            registry.counter("sensor_readings_by_sensor_total", {}, {{"sensor_id", t.sensor_id}})->increment();
            registry.gauge("sensor_temperature_last_celsius", {}, {{"sensor_id", t.sensor_id}})->set(t.temperature);
            registry.gauge("sensor_humidity_last_percent", {}, {{"sensor_id", t.sensor_id}})->set(t.humidity);
            temperature_summary->observe(t.temperature);
            humidity_summary->observe(t.humidity);

            logger.log(telemetry::Severity::Info, "sensor.telemetry.accepted", "telemetry reading accepted",
                {
                    {"sensor_id", t.sensor_id},
                    {"temperature", std::to_string(t.temperature)},
                    {"humidity", std::to_string(t.humidity)}
                },
                request_context(req));

            res.set_content("Telemetry added", "text/plain");
        }
        catch(const std::exception& error)
        {
            diagnostics.record_exception("POST /telemetry", error);
            res.status = 400;
            res.set_content(json({{"error", "invalid telemetry payload"}}).dump(), "application/json");
        }

        active_requests->decrement(1.0);
    });

    svr.Get("/stats", [&](const httplib::Request&, httplib::Response& res) {
        json response;
        response["avg_temperature"] = store.avg_temperature();
        response["count"] = store.size();
        res.set_content(response.dump(), "application/json");
    });

    svr.Get("/telemetry", [&](const httplib::Request&, httplib::Response& res) {
        auto data = store.get_all();
        json response = json::array();

        for(const auto& t : data)
        {
            response.push_back({
                {"sensor_id", t.sensor_id},
                {"temperature", t.temperature},
                {"humidity", t.humidity},
                {"timestamp", t.timestamp}
            });
        }

        res.set_content(response.dump(), "application/json");
    });

    svr.Post("/flush", [&](const httplib::Request&, httplib::Response& res) {
        pipeline.flush();
        res.set_content("flushed", "text/plain");
    });

    std::cout << "Server running on port " << config.monitoring.port << '\n';
    const bool ok = svr.listen(config.monitoring.host.c_str(), config.monitoring.port);

    alert_engine.stop();
    config_manager.stop();
    pipeline.stop();
    return ok ? 0 : 1;
}
