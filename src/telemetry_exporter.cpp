#include "telemetry_exporter.hpp"

namespace telemetry
{

SerializedPayload PrometheusExporter::export_metrics(const std::vector<MetricSnapshot>& metrics) const
{
    const auto text = snapshots_to_prometheus(metrics);
    SerializedPayload payload;
    payload.content_type = "text/plain; version=0.0.4";
    payload.bytes.assign(text.begin(), text.end());
    return payload;
}

JsonMetricExporter::JsonMetricExporter(const Serializer& metric_serializer)
    : serializer(metric_serializer)
{
}

SerializedPayload JsonMetricExporter::export_metrics(const std::vector<MetricSnapshot>& metrics) const
{
    return serializer.serialize_metrics(metrics);
}

} // namespace telemetry
