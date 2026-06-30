#include "telemetry_config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

namespace telemetry
{

namespace
{

int env_int(const char* name, int fallback)
{
    const char* value = std::getenv(name);
    return value ? std::stoi(value) : fallback;
}

std::size_t env_size(const char* name, std::size_t fallback)
{
    const char* value = std::getenv(name);
    return value ? static_cast<std::size_t>(std::stoull(value)) : fallback;
}

double env_double(const char* name, double fallback)
{
    const char* value = std::getenv(name);
    return value ? std::stod(value) : fallback;
}

OverflowPolicy parse_policy(const std::string& value)
{
    if(value == "drop_oldest")
        return OverflowPolicy::DropOldest;
    if(value == "block")
        return OverflowPolicy::Block;
    return OverflowPolicy::DropNewest;
}

std::chrono::milliseconds ms_from_json(const nlohmann::json& source, const char* key, std::chrono::milliseconds fallback)
{
    if(!source.contains(key))
        return fallback;
    return std::chrono::milliseconds(source.at(key).get<int>());
}

} // namespace

TelemetryConfig ConfigLoader::defaults()
{
    TelemetryConfig config;
    config.sinks.push_back(SinkConfig{});
    return config;
}

TelemetryConfig ConfigLoader::from_file(const std::string& path)
{
    TelemetryConfig config = defaults();

    if(path.empty() || !std::filesystem::exists(path))
    {
        apply_environment(config);
        validate(config);
        return config;
    }

    std::ifstream in(path);
    if(!in)
        throw std::runtime_error("unable to open telemetry config: " + path);

    nlohmann::json root;
    in >> root;

    if(root.contains("pipeline"))
    {
        const auto& pipeline = root.at("pipeline");
        config.pipeline.queue_size = pipeline.value("queue_size", config.pipeline.queue_size);
        config.pipeline.batch_size = pipeline.value("batch_size", config.pipeline.batch_size);
        config.pipeline.worker_count = pipeline.value("worker_count", config.pipeline.worker_count);
        config.pipeline.sampling_rate = pipeline.value("sampling_rate", config.pipeline.sampling_rate);
        config.pipeline.rate_limit_per_second = pipeline.value("rate_limit_per_second", config.pipeline.rate_limit_per_second);
        config.pipeline.flush_interval = ms_from_json(pipeline, "flush_interval_ms", config.pipeline.flush_interval);
        config.pipeline.overflow_policy = parse_policy(pipeline.value("overflow_policy", "drop_newest"));
    }

    if(root.contains("monitoring"))
    {
        const auto& monitoring = root.at("monitoring");
        config.monitoring.enabled = monitoring.value("enabled", config.monitoring.enabled);
        config.monitoring.host = monitoring.value("host", config.monitoring.host);
        config.monitoring.port = monitoring.value("port", config.monitoring.port);
    }

    if(root.contains("sinks"))
    {
        config.sinks.clear();
        for(const auto& item : root.at("sinks"))
        {
            SinkConfig sink;
            sink.type = item.value("type", sink.type);
            sink.name = item.value("name", sink.type);
            sink.target = item.value("target", sink.target);
            sink.queue_size = item.value("queue_size", sink.queue_size);
            sink.overflow_policy = parse_policy(item.value("overflow_policy", "drop_oldest"));

            if(item.contains("retry"))
            {
                const auto& retry = item.at("retry");
                sink.retry.max_attempts = retry.value("max_attempts", sink.retry.max_attempts);
                sink.retry.initial_backoff = ms_from_json(retry, "initial_backoff_ms", sink.retry.initial_backoff);
                sink.retry.max_backoff = ms_from_json(retry, "max_backoff_ms", sink.retry.max_backoff);
            }

            config.sinks.push_back(std::move(sink));
        }
    }

    if(root.contains("alerts"))
    {
        config.alerts.clear();
        for(const auto& item : root.at("alerts"))
        {
            AlertRuleConfig rule;
            rule.name = item.value("name", rule.name);
            rule.metric = item.value("metric", rule.metric);
            rule.op = item.value("op", rule.op);
            rule.threshold = item.value("threshold", rule.threshold);
            rule.cooldown = ms_from_json(item, "cooldown_ms", rule.cooldown);
            config.alerts.push_back(std::move(rule));
        }
    }

    apply_environment(config);
    validate(config);
    return config;
}

void ConfigLoader::apply_environment(TelemetryConfig& config)
{
    config.monitoring.port = env_int("TELEMETRY_PORT", env_int("PORT", config.monitoring.port));
    if(const char* token = std::getenv("TELEMETRY_METRICS_TOKEN"))
        config.monitoring.metrics_bearer_token = token;
    config.pipeline.queue_size = env_size("TELEMETRY_QUEUE_SIZE", config.pipeline.queue_size);
    config.pipeline.batch_size = env_size("TELEMETRY_BATCH_SIZE", config.pipeline.batch_size);
    config.pipeline.worker_count = env_size("TELEMETRY_WORKERS", config.pipeline.worker_count);
    config.pipeline.sampling_rate = env_double("TELEMETRY_SAMPLING_RATE", config.pipeline.sampling_rate);
}

void ConfigLoader::validate(const TelemetryConfig& config)
{
    if(config.monitoring.port <= 0 || config.monitoring.port > 65535)
        throw std::invalid_argument("monitoring port must be in range 1..65535");
    if(config.pipeline.queue_size == 0)
        throw std::invalid_argument("pipeline queue_size must be greater than zero");
    if(config.pipeline.batch_size == 0)
        throw std::invalid_argument("pipeline batch_size must be greater than zero");
    if(config.pipeline.worker_count == 0)
        throw std::invalid_argument("pipeline worker_count must be greater than zero");
    if(config.pipeline.sampling_rate < 0.0 || config.pipeline.sampling_rate > 1.0)
        throw std::invalid_argument("pipeline sampling_rate must be in range 0..1");
}

ConfigManager::ConfigManager(std::string config_path, std::function<void(const TelemetryConfig&)> callback)
    : path(std::move(config_path)),
      on_reload(std::move(callback))
{
}

ConfigManager::~ConfigManager()
{
    stop();
}

TelemetryConfig ConfigManager::load_once() const
{
    return ConfigLoader::from_file(path);
}

void ConfigManager::start(std::chrono::milliseconds interval)
{
    if(running.exchange(true))
        return;

    watcher = std::thread([this, interval] {
        std::filesystem::file_time_type last_write{};

        while(running.load())
        {
            try
            {
                if(!path.empty() && std::filesystem::exists(path))
                {
                    const auto current = std::filesystem::last_write_time(path);
                    if(current != last_write)
                    {
                        last_write = current;
                        on_reload(ConfigLoader::from_file(path));
                    }
                }
            }
            catch(...)
            {
            }

            std::this_thread::sleep_for(interval);
        }
    });
}

void ConfigManager::stop()
{
    if(!running.exchange(false))
        return;

    if(watcher.joinable())
        watcher.join();
}

} // namespace telemetry
