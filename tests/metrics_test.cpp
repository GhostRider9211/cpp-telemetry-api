#include "telemetry_metrics.hpp"

#include <cassert>
#include <thread>
#include <vector>

int main()
{
    telemetry::MetricRegistry registry;
    auto counter = registry.counter("requests_total");
    auto gauge = registry.gauge("active_requests");
    auto histogram = registry.histogram("latency_seconds");
    auto summary = registry.summary("payload_size_bytes");

    std::vector<std::thread> threads;
    for(int i = 0; i < 8; ++i)
    {
        threads.emplace_back([&] {
            for(int j = 0; j < 1000; ++j)
            {
                counter->increment();
                histogram->observe(0.01);
                summary->observe(128.0);
            }
        });
    }

    for(auto& thread : threads)
        thread.join();

    gauge->set(3.0);
    assert(counter->value() == 8000);
    assert(gauge->value() == 3.0);

    const auto snapshots = registry.snapshot();
    assert(!snapshots.empty());
    const auto prometheus = telemetry::snapshots_to_prometheus(snapshots);
    assert(prometheus.find("requests_total") != std::string::npos);

    return 0;
}
