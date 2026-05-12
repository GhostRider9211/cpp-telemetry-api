#include "telemetry_serializer.hpp"

#include <nlohmann/json.hpp>

namespace telemetry
{

namespace
{

nlohmann::json labels_to_json(const Labels& labels)
{
    nlohmann::json result = nlohmann::json::object();
    for(const auto& label : labels)
        result[label.first] = label.second;
    return result;
}

nlohmann::json metric_to_json(const MetricSnapshot& metric)
{
    nlohmann::json item;
    item["name"] = metric.name;
    item["type"] = metric_type_name(metric.type);
    item["description"] = metric.description;
    item["unit"] = metric.unit;
    item["labels"] = labels_to_json(metric.labels);
    item["value"] = metric.value;
    item["count"] = metric.count;
    item["sum"] = metric.sum;

    if(!metric.buckets.empty())
    {
        item["buckets"] = nlohmann::json::array();
        for(const auto& bucket : metric.buckets)
            item["buckets"].push_back({{"le", bucket.first}, {"count", bucket.second}});
    }

    if(!metric.quantiles.empty())
    {
        item["quantiles"] = nlohmann::json::array();
        for(const auto& quantile : metric.quantiles)
            item["quantiles"].push_back({{"quantile", quantile.first}, {"value", quantile.second}});
    }

    return item;
}

SerializedPayload make_json_payload(nlohmann::json root)
{
    auto encoded = root.dump();
    SerializedPayload payload;
    payload.content_type = "application/json";
    payload.bytes.assign(encoded.begin(), encoded.end());
    return payload;
}

} // namespace

SerializedPayload JsonSerializer::serialize_events(const EventBatch& events) const
{
    nlohmann::json root;
    root["schema"] = "telemetry.events.v1";
    root["events"] = nlohmann::json::array();

    for(const auto& event : events)
    {
        nlohmann::json item;
        item["name"] = event.name;
        item["severity"] = severity_name(event.severity);
        item["message"] = event.message;
        item["timestamp"] = iso8601_utc(event.timestamp);
        item["correlation_id"] = event.correlation_id;
        item["request_id"] = event.request_id;
        item["process_id"] = event.process_id;
        item["thread_id"] = event.thread_id;
        item["host"] = event.host;
        item["fields"] = event.fields;
        root["events"].push_back(std::move(item));
    }

    return make_json_payload(std::move(root));
}

SerializedPayload JsonSerializer::serialize_metrics(const std::vector<MetricSnapshot>& metrics) const
{
    nlohmann::json root;
    root["schema"] = "telemetry.metrics.v1";
    root["metrics"] = nlohmann::json::array();

    for(const auto& metric : metrics)
        root["metrics"].push_back(metric_to_json(metric));

    return make_json_payload(std::move(root));
}

std::string JsonSerializer::content_type() const
{
    return "application/json";
}

std::shared_ptr<Serializer> make_json_serializer()
{
    return std::make_shared<JsonSerializer>();
}

std::string payload_to_string(const SerializedPayload& payload)
{
    return std::string(payload.bytes.begin(), payload.bytes.end());
}

} // namespace telemetry
