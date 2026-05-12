#pragma once

#include "telemetry_event.hpp"
#include "telemetry_metrics.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace telemetry
{

struct SerializedPayload
{
    std::vector<std::uint8_t> bytes;
    std::string content_type{"application/octet-stream"};
};

class Serializer
{
public:
    virtual ~Serializer() = default;

    virtual SerializedPayload serialize_events(const EventBatch& events) const = 0;
    virtual SerializedPayload serialize_metrics(const std::vector<MetricSnapshot>& metrics) const = 0;
    virtual std::string content_type() const = 0;
};

class JsonSerializer final : public Serializer
{
public:
    SerializedPayload serialize_events(const EventBatch& events) const override;
    SerializedPayload serialize_metrics(const std::vector<MetricSnapshot>& metrics) const override;
    std::string content_type() const override;
};

class ProtobufReadySerializer : public Serializer
{
public:
    ~ProtobufReadySerializer() override = default;
};

class MessagePackReadySerializer : public Serializer
{
public:
    ~MessagePackReadySerializer() override = default;
};

std::shared_ptr<Serializer> make_json_serializer();
std::string payload_to_string(const SerializedPayload& payload);

} // namespace telemetry
