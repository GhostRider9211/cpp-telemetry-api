#include "telemetry_monitoring.hpp"

#include <memory>
#include <nlohmann/json.hpp>

namespace telemetry
{

void register_monitoring_routes(
    httplib::Server& server,
    MetricRegistry& registry,
    TelemetryPipeline& pipeline,
    Diagnostics& diagnostics,
    const Serializer& serializer)
{
    auto prometheus_exporter = std::make_shared<PrometheusExporter>();
    auto otlp_exporter = std::make_shared<OtlpJsonMetricExporter>(serializer);

    server.Get("/health", [&diagnostics](const httplib::Request&, httplib::Response& res) {
        const auto runtime = diagnostics.snapshot();
        nlohmann::json body;
        body["status"] = "ok";
        body["process_id"] = runtime.process_id;
        body["host"] = runtime.host;
        body["thread_id"] = runtime.thread_id;
        body["uptime_ms"] = runtime.uptime_ms;
        res.set_content(body.dump(), "application/json");
    });

    server.Get("/metrics", [&registry, prometheus_exporter](const httplib::Request&, httplib::Response& res) {
        const auto payload = prometheus_exporter->export_metrics(registry.snapshot());
        res.set_content(payload_to_string(payload), payload.content_type);
    });

    server.Get("/otlp/v1/metrics", [&registry, otlp_exporter](const httplib::Request&, httplib::Response& res) {
        const auto payload = otlp_exporter->export_metrics(registry.snapshot());
        res.set_content(payload_to_string(payload), payload.content_type);
    });

    server.Get("/telemetry/live", [&pipeline](const httplib::Request&, httplib::Response& res) {
        auto subscription = pipeline.streams().subscribe();
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");
        res.set_chunked_content_provider("text/event-stream",
            [subscription](std::size_t, httplib::DataSink& sink) {
                std::string event;
                if(subscription->pop_for(event, std::chrono::seconds(15)))
                {
                    return sink.write(event.data(), event.size());
                }

                static const std::string heartbeat = ": heartbeat\n\n";
                return sink.write(heartbeat.data(), heartbeat.size());
            },
            [subscription](bool) {
                subscription->close();
            });
    });
}

} // namespace telemetry
