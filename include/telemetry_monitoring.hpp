#pragma once

#include "httplib.h"
#include "telemetry_diagnostics.hpp"
#include "telemetry_exporter.hpp"
#include "telemetry_metrics.hpp"
#include "telemetry_pipeline.hpp"
#include "telemetry_serializer.hpp"

namespace telemetry
{

void register_monitoring_routes(
    httplib::Server& server,
    MetricRegistry& registry,
    TelemetryPipeline& pipeline,
    Diagnostics& diagnostics,
    const Serializer& serializer);

} // namespace telemetry
