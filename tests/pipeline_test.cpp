#include "telemetry_pipeline.hpp"

#include <cassert>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

class MemorySink final : public telemetry::Sink
{
private:
    std::string sink_name{"memory"};
    mutable std::mutex mtx;
    std::vector<std::string> payloads;

public:
    const std::string& name() const override
    {
        return sink_name;
    }

    bool write(const telemetry::SerializedPayload& payload) override
    {
        std::lock_guard<std::mutex> lock(mtx);
        payloads.emplace_back(payload.bytes.begin(), payload.bytes.end());
        return true;
    }

    std::size_t size() const
    {
        return payloads.size();
    }
};

int main()
{
    telemetry::MetricRegistry registry;
    telemetry::PipelineConfig config;
    config.queue_size = 128;
    config.batch_size = 8;
    config.worker_count = 1;
    config.flush_interval = std::chrono::milliseconds(20);

    auto sink = std::make_shared<MemorySink>();
    telemetry::SinkConfig sink_config;
    sink_config.queue_size = 64;

    telemetry::TelemetryPipeline pipeline(config, telemetry::make_json_serializer(), registry);
    pipeline.add_sink(sink, sink_config);
    pipeline.start();

    telemetry::EventLogger logger(pipeline);
    for(int i = 0; i < 20; ++i)
        assert(logger.info("test.event", "pipeline test"));

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    pipeline.stop();

    assert(sink->size() > 0);
    assert(telemetry::snapshots_to_prometheus(registry.snapshot()).find("telemetry_events_accepted_total") != std::string::npos);
    return 0;
}
