#pragma once

#include "telemetry_queue.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace telemetry
{

struct RetryConfig
{
    int max_attempts{3};
    std::chrono::milliseconds initial_backoff{std::chrono::milliseconds(50)};
    std::chrono::milliseconds max_backoff{std::chrono::seconds(2)};
};

struct PipelineConfig
{
    std::size_t queue_size{8192};
    std::size_t batch_size{128};
    std::size_t worker_count{2};
    std::chrono::milliseconds flush_interval{std::chrono::milliseconds(1000)};
    OverflowPolicy overflow_policy{OverflowPolicy::DropNewest};
    double sampling_rate{1.0};
    std::size_t rate_limit_per_second{0};
};

struct SinkConfig
{
    std::string type{"stdout"};
    std::string name{"stdout"};
    std::string target;
    std::size_t queue_size{4096};
    OverflowPolicy overflow_policy{OverflowPolicy::DropOldest};
    RetryConfig retry;
};

struct MonitoringConfig
{
    bool enabled{true};
    std::string host{"0.0.0.0"};
    int port{8080};
};

struct AlertRuleConfig
{
    std::string name;
    std::string metric;
    std::string op{">"};
    double threshold{0.0};
    std::chrono::milliseconds cooldown{std::chrono::seconds(30)};
};

struct TelemetryConfig
{
    PipelineConfig pipeline;
    MonitoringConfig monitoring;
    std::vector<SinkConfig> sinks;
    std::vector<AlertRuleConfig> alerts;
};

class ConfigLoader
{
public:
    static TelemetryConfig defaults();
    static TelemetryConfig from_file(const std::string& path);
    static void apply_environment(TelemetryConfig& config);
    static void validate(const TelemetryConfig& config);
};

class ConfigManager
{
private:
    std::string path;
    std::function<void(const TelemetryConfig&)> on_reload;
    std::atomic<bool> running{false};
    std::thread watcher;

public:
    ConfigManager(std::string config_path, std::function<void(const TelemetryConfig&)> callback);
    ~ConfigManager();

    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    TelemetryConfig load_once() const;
    void start(std::chrono::milliseconds interval = std::chrono::seconds(2));
    void stop();
};

} // namespace telemetry
