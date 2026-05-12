#include "telemetry_event.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace telemetry
{

std::string severity_name(Severity severity)
{
    switch(severity)
    {
    case Severity::Trace:
        return "trace";
    case Severity::Debug:
        return "debug";
    case Severity::Info:
        return "info";
    case Severity::Warning:
        return "warning";
    case Severity::Error:
        return "error";
    case Severity::Critical:
        return "critical";
    }

    return "info";
}

Severity severity_from_string(const std::string& value)
{
    std::string normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if(normalized == "trace")
        return Severity::Trace;
    if(normalized == "debug")
        return Severity::Debug;
    if(normalized == "warning" || normalized == "warn")
        return Severity::Warning;
    if(normalized == "error")
        return Severity::Error;
    if(normalized == "critical" || normalized == "fatal")
        return Severity::Critical;
    return Severity::Info;
}

std::string iso8601_utc(std::chrono::system_clock::time_point timestamp)
{
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(timestamp);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp - seconds).count();
    const std::time_t time = std::chrono::system_clock::to_time_t(seconds);

    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif

    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setw(3) << std::setfill('0') << millis << 'Z';
    return out.str();
}

Event enrich_event(Event event, const EventContext& context)
{
    if(event.timestamp.time_since_epoch().count() == 0)
        event.timestamp = std::chrono::system_clock::now();

    if(event.correlation_id.empty())
        event.correlation_id = context.correlation_id;
    if(event.request_id.empty())
        event.request_id = context.request_id;

    for(const auto& item : context.metadata)
        event.fields.emplace(item.first, item.second);

    event.process_id = current_process_id();
    event.thread_id = current_thread_id();
    event.host = current_host_name();
    return event;
}

std::string current_host_name()
{
    char name[256] = {};
#ifdef _WIN32
    DWORD size = sizeof(name);
    if(GetComputerNameA(name, &size))
        return std::string(name, size);
#else
    if(gethostname(name, sizeof(name)) == 0)
        return name;
#endif
    return "unknown";
}

std::uint32_t current_process_id()
{
#ifdef _WIN32
    return static_cast<std::uint32_t>(_getpid());
#else
    return static_cast<std::uint32_t>(getpid());
#endif
}

std::string current_thread_id()
{
    std::ostringstream out;
    out << std::this_thread::get_id();
    return out.str();
}

} // namespace telemetry
