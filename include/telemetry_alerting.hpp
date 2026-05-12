#pragma once

#include "telemetry_config.hpp"
#include "telemetry_metrics.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace telemetry
{

enum class AlertOperator
{
    GreaterThan,
    GreaterOrEqual,
    LessThan,
    LessOrEqual,
    Equal
};

struct Alert
{
    std::string rule_name;
    std::string metric_name;
    double value{0.0};
    double threshold{0.0};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct AlertRule
{
    std::string name;
    std::string metric_name;
    AlertOperator op{AlertOperator::GreaterThan};
    double threshold{0.0};
    std::chrono::milliseconds cooldown{std::chrono::seconds(30)};
};

class AlertNotifier
{
public:
    virtual ~AlertNotifier() = default;
    virtual bool notify(const Alert& alert) = 0;
};

class StdoutAlertNotifier final : public AlertNotifier
{
public:
    bool notify(const Alert& alert) override;
};

class WebhookAlertNotifier final : public AlertNotifier
{
private:
    std::string url;

public:
    explicit WebhookAlertNotifier(std::string endpoint);
    bool notify(const Alert& alert) override;
};

class AlertEngine
{
private:
    MetricRegistry& registry;
    std::vector<AlertRule> rules;
    std::vector<std::shared_ptr<AlertNotifier>> notifiers;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_fired;
    std::mutex mtx;
    std::atomic<bool> running{false};
    std::thread worker;

    void evaluate_once_locked();

public:
    explicit AlertEngine(MetricRegistry& metric_registry);
    ~AlertEngine();

    AlertEngine(const AlertEngine&) = delete;
    AlertEngine& operator=(const AlertEngine&) = delete;

    void add_rule(AlertRule rule);
    void add_rule(const AlertRuleConfig& rule);
    void add_notifier(std::shared_ptr<AlertNotifier> notifier);
    void start(std::chrono::milliseconds interval = std::chrono::seconds(5));
    void stop();
    void evaluate_once();
};

AlertOperator parse_alert_operator(const std::string& value);

} // namespace telemetry
