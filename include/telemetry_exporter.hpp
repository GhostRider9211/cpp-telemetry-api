#pragma once

#include "telemetry_metrics.hpp"
#include "telemetry_serializer.hpp"

#include <string>
#include <vector>

namespace telemetry
{

class MetricExporter
{
public:
    virtual ~MetricExporter() = default;
    virtual SerializedPayload export_metrics(const std::vector<MetricSnapshot>& metrics) const = 0;
};

class PrometheusExporter final : public MetricExporter
{
public:
    SerializedPayload export_metrics(const std::vector<MetricSnapshot>& metrics) const override;
};

class JsonMetricExporter final : public MetricExporter
{
private:
    const Serializer& serializer;

public:
    explicit JsonMetricExporter(const Serializer& metric_serializer);
    SerializedPayload export_metrics(const std::vector<MetricSnapshot>& metrics) const override;
};

} // namespace telemetry
