#include "telemetry_diagnostics.hpp"

#include "telemetry_event.hpp"

#include <cstdlib>
#include <exception>
#include <utility>

namespace telemetry
{

std::atomic<Diagnostics*> Diagnostics::installed{nullptr};

Diagnostics::Diagnostics(MetricRegistry& metric_registry)
    : registry(metric_registry)
{
    exceptions_counter = registry.counter("telemetry_exceptions_total", {"Exceptions captured by telemetry diagnostics"});
    uptime_gauge = registry.gauge("telemetry_process_uptime_ms", {"Process uptime in milliseconds", "ms"});
}

Diagnostics::~Diagnostics()
{
    Diagnostics* expected = this;
    installed.compare_exchange_strong(expected, nullptr);
}

void Diagnostics::attach_logger(EventLogger& event_logger)
{
    logger = &event_logger;
}

RuntimeDiagnostics Diagnostics::snapshot()
{
    const auto uptime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at).count();
    uptime_gauge->set(static_cast<double>(uptime));
    return RuntimeDiagnostics{
        current_process_id(),
        current_host_name(),
        current_thread_id(),
        static_cast<std::uint64_t>(uptime)};
}

void Diagnostics::record_exception(const std::string& source, const std::exception& error)
{
    exceptions_counter->increment();
    if(logger)
    {
        logger->error("exception", error.what(), {
            {"source", source}
        });
    }
}

void Diagnostics::install_terminate_handler()
{
    installed.store(this);
    std::set_terminate(&Diagnostics::handle_terminate);
}

void Diagnostics::handle_terminate()
{
    if(auto* diagnostics = installed.load())
    {
        diagnostics->exceptions_counter->increment();
        if(diagnostics->logger)
            diagnostics->logger->critical("terminate", "unhandled exception triggered terminate");
    }

    std::abort();
}

} // namespace telemetry
