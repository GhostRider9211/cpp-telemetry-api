#include "telemetry_pipeline.hpp"

#include <chrono>
#include <nlohmann/json.hpp>
#include <utility>

namespace telemetry
{

TelemetryStreamHub::Subscription::Subscription(std::size_t queue_size)
    : queue(queue_size)
{
}

bool TelemetryStreamHub::Subscription::pop_for(std::string& item, std::chrono::milliseconds timeout)
{
    return active.load() && queue.pop_for(item, timeout);
}

void TelemetryStreamHub::Subscription::close()
{
    active.store(false);
    queue.close();
}

std::shared_ptr<TelemetryStreamHub::Subscription> TelemetryStreamHub::subscribe()
{
    auto subscription = std::shared_ptr<Subscription>(new Subscription(subscription_queue_size));
    std::lock_guard<std::mutex> lock(mtx);
    subscriptions.push_back(subscription);
    return subscription;
}

void TelemetryStreamHub::publish(const std::string& payload)
{
    std::lock_guard<std::mutex> lock(mtx);
    auto it = subscriptions.begin();
    while(it != subscriptions.end())
    {
        if(auto subscription = it->lock())
        {
            subscription->queue.try_push(payload, OverflowPolicy::DropOldest);
            ++it;
        }
        else
        {
            it = subscriptions.erase(it);
        }
    }
}

TelemetryPipeline::TelemetryPipeline(PipelineConfig pipeline_config, std::shared_ptr<Serializer> event_serializer, MetricRegistry& metric_registry)
    : config(std::move(pipeline_config)),
      queue(config.queue_size),
      serializer(std::move(event_serializer)),
      registry(metric_registry),
      batch_size(config.batch_size),
      flush_interval_ms(static_cast<std::uint64_t>(config.flush_interval.count())),
      sampling_rate(config.sampling_rate),
      rate_limit_per_second(config.rate_limit_per_second)
{
    accepted_counter = registry.counter("telemetry_events_accepted_total", {"Accepted telemetry events"});
    dropped_counter = registry.counter("telemetry_events_dropped_total", {"Dropped telemetry events"});
    sampled_counter = registry.counter("telemetry_events_sampled_total", {"Events rejected by sampling"});
    batches_counter = registry.counter("telemetry_batches_exported_total", {"Exported telemetry batches"});
    sink_dropped_counter = registry.counter("telemetry_sink_queue_dropped_total", {"Payloads dropped by sink queues"});
    batch_latency = registry.summary("telemetry_batch_export_seconds", {"Batch export worker duration", "seconds"});
}

TelemetryPipeline::~TelemetryPipeline()
{
    stop();
}

void TelemetryPipeline::add_sink(std::shared_ptr<Sink> sink, const SinkConfig& sink_config)
{
    sink_workers.emplace_back(std::make_unique<SinkWorker>(
        std::move(sink),
        sink_config.queue_size,
        sink_config.overflow_policy,
        sink_config.retry));
}

void TelemetryPipeline::start()
{
    if(running.exchange(true))
        return;

    for(auto& sink : sink_workers)
        sink->start();

    workers.reserve(config.worker_count);
    for(std::size_t i = 0; i < config.worker_count; ++i)
        workers.emplace_back([this] { worker_loop(); });
}

void TelemetryPipeline::stop()
{
    if(!running.exchange(false))
        return;

    queue.close();
    for(auto& worker : workers)
    {
        if(worker.joinable())
            worker.join();
    }
    workers.clear();

    for(auto& sink : sink_workers)
        sink->stop();
}

bool TelemetryPipeline::sampled_in()
{
    const auto current_rate = sampling_rate.load(std::memory_order_relaxed);
    if(current_rate >= 1.0)
        return true;
    if(current_rate <= 0.0)
        return false;

    thread_local std::mt19937_64 rng(std::random_device{}());
    std::uniform_real_distribution<double> distribution(0.0, 1.0);
    return distribution(rng) <= current_rate;
}

bool TelemetryPipeline::within_rate_limit()
{
    const auto current_limit = rate_limit_per_second.load(std::memory_order_relaxed);
    if(current_limit == 0)
        return true;

    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto second = static_cast<std::uint64_t>(now);

    auto observed = token_second.load(std::memory_order_relaxed);
    if(observed != second && token_second.compare_exchange_strong(observed, second))
        token_count.store(0, std::memory_order_relaxed);

    const auto current = token_count.fetch_add(1, std::memory_order_relaxed);
    return current < current_limit;
}

bool TelemetryPipeline::enqueue(Event event)
{
    if(!running.load(std::memory_order_relaxed))
        return false;

    if(!sampled_in())
    {
        sampled_counter->increment();
        return false;
    }

    if(!within_rate_limit())
    {
        dropped_counter->increment();
        return false;
    }

    event = enrich_event(std::move(event));

    std::size_t dropped_now = 0;
    const auto accepted = queue.try_push(std::move(event), config.overflow_policy, &dropped_now);
    if(accepted)
        accepted_counter->increment();
    if(dropped_now > 0 || !accepted)
        dropped_counter->increment(dropped_now > 0 ? dropped_now : 1);

    return accepted;
}

void TelemetryPipeline::worker_loop()
{
    EventBatch batch;
    batch.reserve(batch_size.load(std::memory_order_relaxed));
    Event event;

    while(running.load(std::memory_order_relaxed))
    {
        const auto current_batch_size = batch_size.load(std::memory_order_relaxed);
        const auto current_flush = std::chrono::milliseconds(flush_interval_ms.load(std::memory_order_relaxed));

        if(queue.pop_for(event, current_flush))
        {
            batch.push_back(std::move(event));
            while(batch.size() < current_batch_size && queue.pop_for(event, std::chrono::milliseconds(0)))
                batch.push_back(std::move(event));
        }

        if(!batch.empty())
        {
            Timer timer(*batch_latency);
            publish_batch(batch);
            batch.clear();
        }
    }

    while(queue.pop_for(event, std::chrono::milliseconds(0)))
    {
        batch.push_back(std::move(event));
        if(batch.size() >= batch_size.load(std::memory_order_relaxed))
        {
            publish_batch(batch);
            batch.clear();
        }
    }

    if(!batch.empty())
        publish_batch(batch);
}

void TelemetryPipeline::publish_batch(const EventBatch& batch)
{
    if(batch.empty())
        return;

    const auto payload = std::make_shared<SerializedPayload>(serializer->serialize_events(batch));
    batches_counter->increment();

    nlohmann::json stream_root;
    stream_root["schema"] = "telemetry.events.v1";
    stream_root["events"] = nlohmann::json::array();
    for(const auto& event : batch)
    {
        stream_root["events"].push_back({
            {"name", event.name},
            {"severity", severity_name(event.severity)},
            {"message", event.message},
            {"timestamp", iso8601_utc(event.timestamp)},
            {"correlation_id", event.correlation_id},
            {"request_id", event.request_id},
            {"fields", event.fields}});
    }
    stream_hub.publish("data: " + stream_root.dump() + "\n\n");

    for(auto& sink : sink_workers)
        sink->submit(payload);

    update_sink_metrics();
}

void TelemetryPipeline::update_sink_metrics()
{
    std::uint64_t total_dropped = 0;
    for(const auto& sink : sink_workers)
        total_dropped += sink->stats().dropped;

    const auto previous = last_sink_dropped.exchange(total_dropped, std::memory_order_relaxed);
    if(total_dropped > previous)
        sink_dropped_counter->increment(total_dropped - previous);
}

void TelemetryPipeline::flush()
{
    for(auto& sink : sink_workers)
        sink->flush();
}

void TelemetryPipeline::update_runtime_config(const PipelineConfig& pipeline_config)
{
    batch_size.store(pipeline_config.batch_size == 0 ? 1 : pipeline_config.batch_size, std::memory_order_relaxed);
    flush_interval_ms.store(static_cast<std::uint64_t>(pipeline_config.flush_interval.count()), std::memory_order_relaxed);
    sampling_rate.store(pipeline_config.sampling_rate, std::memory_order_relaxed);
    rate_limit_per_second.store(pipeline_config.rate_limit_per_second, std::memory_order_relaxed);
}

TelemetryStreamHub& TelemetryPipeline::streams()
{
    return stream_hub;
}

const PipelineConfig& TelemetryPipeline::current_config() const
{
    return config;
}

EventLogger::EventLogger(TelemetryPipeline& telemetry_pipeline, EventContext context)
    : pipeline(telemetry_pipeline),
      base_context(std::move(context))
{
}

bool EventLogger::log(Severity severity, std::string name, std::string message, EventFields fields, EventContext context)
{
    for(const auto& item : base_context.metadata)
        context.metadata.emplace(item.first, item.second);
    if(context.correlation_id.empty())
        context.correlation_id = base_context.correlation_id;
    if(context.request_id.empty())
        context.request_id = base_context.request_id;

    Event event;
    event.severity = severity;
    event.name = std::move(name);
    event.message = std::move(message);
    event.fields = std::move(fields);
    event.correlation_id = context.correlation_id;
    event.request_id = context.request_id;
    for(const auto& item : context.metadata)
        event.fields.emplace(item.first, item.second);

    return pipeline.enqueue(std::move(event));
}

bool EventLogger::trace(std::string name, std::string message, EventFields fields)
{
    return log(Severity::Trace, std::move(name), std::move(message), std::move(fields));
}

bool EventLogger::debug(std::string name, std::string message, EventFields fields)
{
    return log(Severity::Debug, std::move(name), std::move(message), std::move(fields));
}

bool EventLogger::info(std::string name, std::string message, EventFields fields)
{
    return log(Severity::Info, std::move(name), std::move(message), std::move(fields));
}

bool EventLogger::warning(std::string name, std::string message, EventFields fields)
{
    return log(Severity::Warning, std::move(name), std::move(message), std::move(fields));
}

bool EventLogger::error(std::string name, std::string message, EventFields fields)
{
    return log(Severity::Error, std::move(name), std::move(message), std::move(fields));
}

bool EventLogger::critical(std::string name, std::string message, EventFields fields)
{
    return log(Severity::Critical, std::move(name), std::move(message), std::move(fields));
}

} // namespace telemetry
