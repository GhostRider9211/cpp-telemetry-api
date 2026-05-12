#pragma once

#include "telemetry_pipeline.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace telemetry
{

struct RuntimeDiagnostics
{
    std::uint32_t process_id{0};
    std::string host;
    std::string thread_id;
    std::uint64_t uptime_ms{0};
};

class Diagnostics
{
private:
    std::chrono::steady_clock::time_point started_at{std::chrono::steady_clock::now()};
    MetricRegistry& registry;
    EventLogger* logger{nullptr};
    std::shared_ptr<Counter> exceptions_counter;
    std::shared_ptr<Gauge> uptime_gauge;
    static std::atomic<Diagnostics*> installed;

public:
    explicit Diagnostics(MetricRegistry& metric_registry);
    ~Diagnostics();

    Diagnostics(const Diagnostics&) = delete;
    Diagnostics& operator=(const Diagnostics&) = delete;

    void attach_logger(EventLogger& event_logger);
    RuntimeDiagnostics snapshot();
    void record_exception(const std::string& source, const std::exception& error);
    void install_terminate_handler();
    static void handle_terminate();
};

} // namespace telemetry
