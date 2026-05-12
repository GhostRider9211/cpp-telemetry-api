#include "telemetry_serializer.hpp"

#include <cassert>
#include <string>

int main()
{
    telemetry::Event event;
    event.name = "serializer.test";
    event.message = "ok";
    event.fields.emplace("key", "value");
    event = telemetry::enrich_event(std::move(event));

    telemetry::JsonSerializer serializer;
    auto payload = serializer.serialize_events({event});
    auto text = telemetry::payload_to_string(payload);

    assert(payload.content_type == "application/json");
    assert(text.find("telemetry.events.v1") != std::string::npos);
    assert(text.find("serializer.test") != std::string::npos);

    telemetry::MetricRegistry registry;
    registry.counter("items_total")->increment(2);
    auto metrics = serializer.serialize_metrics(registry.snapshot());
    assert(telemetry::payload_to_string(metrics).find("items_total") != std::string::npos);

    return 0;
}
