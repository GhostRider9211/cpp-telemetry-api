#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace telemetry
{

enum class Severity
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical
};

using EventFields = std::unordered_map<std::string, std::string>;

struct EventContext
{
    std::string correlation_id;
    std::string request_id;
    EventFields metadata;
};

struct Event
{
    std::string name;
    Severity severity{Severity::Info};
    std::string message;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
    EventFields fields;
    std::string correlation_id;
    std::string request_id;
    std::uint32_t process_id{0};
    std::string thread_id;
    std::string host;
};

using EventBatch = std::vector<Event>;

std::string severity_name(Severity severity);
Severity severity_from_string(const std::string& value);
std::string iso8601_utc(std::chrono::system_clock::time_point timestamp);
Event enrich_event(Event event, const EventContext& context = {});
std::string current_host_name();
std::uint32_t current_process_id();
std::string current_thread_id();

} // namespace telemetry
