#pragma once

#include "telemetry_config.hpp"
#include "telemetry_event.hpp"
#include "telemetry_metrics.hpp"
#include "telemetry_queue.hpp"
#include "telemetry_serializer.hpp"
#include "telemetry_sinks.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace telemetry
{

class TelemetryStreamHub
{
public:
    class Subscription
    {
    private:
        friend class TelemetryStreamHub;
        BoundedQueue<std::string> queue;
        std::atomic<bool> active{true};

        explicit Subscription(std::size_t queue_size);

    public:
        bool pop_for(std::string& item, std::chrono::milliseconds timeout);
        void close();
    };

private:
    mutable std::mutex mtx;
    std::vector<std::weak_ptr<Subscription>> subscriptions;
    std::size_t subscription_queue_size{128};

public:
    std::shared_ptr<Subscription> subscribe();
    void publish(const std::string& payload);
};

class TelemetryPipeline
{
private:
    PipelineConfig config;
    BoundedQueue<Event> queue;
    std::shared_ptr<Serializer> serializer;
    MetricRegistry& registry;
    TelemetryStreamHub stream_hub;
    std::vector<std::unique_ptr<SinkWorker>> sink_workers;
    std::vector<std::thread> workers;
    std::atomic<bool> running{false};
    std::atomic<std::size_t> batch_size;
    std::atomic<std::uint64_t> flush_interval_ms;
    std::atomic<double> sampling_rate;
    std::atomic<std::size_t> rate_limit_per_second;
    std::atomic<std::uint64_t> token_second{0};
    std::atomic<std::size_t> token_count{0};

    std::shared_ptr<Counter> accepted_counter;
    std::shared_ptr<Counter> dropped_counter;
    std::shared_ptr<Counter> sampled_counter;
    std::shared_ptr<Counter> batches_counter;
    std::shared_ptr<Counter> sink_dropped_counter;
    std::shared_ptr<Summary> batch_latency;
    std::atomic<std::uint64_t> last_sink_dropped{0};

    bool sampled_in();
    bool within_rate_limit();
    void worker_loop();
    void publish_batch(const EventBatch& batch);
    void update_sink_metrics();

public:
    TelemetryPipeline(PipelineConfig pipeline_config, std::shared_ptr<Serializer> event_serializer, MetricRegistry& metric_registry);
    ~TelemetryPipeline();

    TelemetryPipeline(const TelemetryPipeline&) = delete;
    TelemetryPipeline& operator=(const TelemetryPipeline&) = delete;

    void add_sink(std::shared_ptr<Sink> sink, const SinkConfig& sink_config);
    void start();
    void stop();
    void update_runtime_config(const PipelineConfig& pipeline_config);
    bool enqueue(Event event);
    void flush();
    TelemetryStreamHub& streams();
    const PipelineConfig& current_config() const;
};

class EventLogger
{
private:
    TelemetryPipeline& pipeline;
    EventContext base_context;

public:
    explicit EventLogger(TelemetryPipeline& telemetry_pipeline, EventContext context = {});

    bool log(Severity severity, std::string name, std::string message, EventFields fields = {}, EventContext context = {});
    bool trace(std::string name, std::string message, EventFields fields = {});
    bool debug(std::string name, std::string message, EventFields fields = {});
    bool info(std::string name, std::string message, EventFields fields = {});
    bool warning(std::string name, std::string message, EventFields fields = {});
    bool error(std::string name, std::string message, EventFields fields = {});
    bool critical(std::string name, std::string message, EventFields fields = {});
};

} // namespace telemetry
