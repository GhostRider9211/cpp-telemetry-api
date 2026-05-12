#include "telemetry_alerting.hpp"

#include "httplib.h"
#include "telemetry_event.hpp"

#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
#include <utility>

namespace telemetry
{

namespace
{

bool compare(AlertOperator op, double value, double threshold)
{
    switch(op)
    {
    case AlertOperator::GreaterThan:
        return value > threshold;
    case AlertOperator::GreaterOrEqual:
        return value >= threshold;
    case AlertOperator::LessThan:
        return value < threshold;
    case AlertOperator::LessOrEqual:
        return value <= threshold;
    case AlertOperator::Equal:
        return value == threshold;
    }

    return false;
}

double alert_value(const MetricSnapshot& metric)
{
    if(metric.type == MetricType::Histogram || metric.type == MetricType::Summary)
        return metric.count == 0 ? 0.0 : metric.sum / static_cast<double>(metric.count);
    return metric.value;
}

struct ParsedUrl
{
    std::string host;
    int port{80};
    std::string path{"/"};
};

ParsedUrl parse_url(const std::string& url)
{
    std::string value = url;
    if(value.rfind("http://", 0) == 0)
        value.erase(0, 7);

    ParsedUrl parsed;
    const auto slash = value.find('/');
    const auto host_port = slash == std::string::npos ? value : value.substr(0, slash);
    parsed.path = slash == std::string::npos ? "/" : value.substr(slash);

    const auto colon = host_port.find(':');
    if(colon == std::string::npos)
    {
        parsed.host = host_port;
    }
    else
    {
        parsed.host = host_port.substr(0, colon);
        parsed.port = std::stoi(host_port.substr(colon + 1));
    }

    return parsed;
}

} // namespace

bool StdoutAlertNotifier::notify(const Alert& alert)
{
    std::cout << "ALERT " << alert.rule_name << " metric=" << alert.metric_name
              << " value=" << alert.value << " threshold=" << alert.threshold << '\n';
    return static_cast<bool>(std::cout);
}

WebhookAlertNotifier::WebhookAlertNotifier(std::string endpoint)
    : url(std::move(endpoint))
{
}

bool WebhookAlertNotifier::notify(const Alert& alert)
{
    const auto parsed = parse_url(url);
    httplib::Client client(parsed.host, parsed.port);

    nlohmann::json body;
    body["rule"] = alert.rule_name;
    body["metric"] = alert.metric_name;
    body["value"] = alert.value;
    body["threshold"] = alert.threshold;
    body["timestamp"] = iso8601_utc(alert.timestamp);

    auto result = client.Post(parsed.path.c_str(), body.dump(), "application/json");
    return result && result->status >= 200 && result->status < 300;
}

AlertEngine::AlertEngine(MetricRegistry& metric_registry)
    : registry(metric_registry)
{
}

AlertEngine::~AlertEngine()
{
    stop();
}

void AlertEngine::add_rule(AlertRule rule)
{
    std::lock_guard<std::mutex> lock(mtx);
    rules.push_back(std::move(rule));
}

void AlertEngine::add_rule(const AlertRuleConfig& rule)
{
    add_rule(AlertRule{rule.name, rule.metric, parse_alert_operator(rule.op), rule.threshold, rule.cooldown});
}

void AlertEngine::add_notifier(std::shared_ptr<AlertNotifier> notifier)
{
    std::lock_guard<std::mutex> lock(mtx);
    notifiers.push_back(std::move(notifier));
}

void AlertEngine::start(std::chrono::milliseconds interval)
{
    if(running.exchange(true))
        return;

    worker = std::thread([this, interval] {
        while(running.load())
        {
            evaluate_once();
            std::this_thread::sleep_for(interval);
        }
    });
}

void AlertEngine::stop()
{
    if(!running.exchange(false))
        return;

    if(worker.joinable())
        worker.join();
}

void AlertEngine::evaluate_once()
{
    std::lock_guard<std::mutex> lock(mtx);
    evaluate_once_locked();
}

void AlertEngine::evaluate_once_locked()
{
    const auto snapshots = registry.snapshot();
    const auto now = std::chrono::steady_clock::now();

    for(const auto& rule : rules)
    {
        for(const auto& metric : snapshots)
        {
            if(metric.name != rule.metric_name)
                continue;

            const auto value = alert_value(metric);
            if(!compare(rule.op, value, rule.threshold))
                continue;

            const auto fired = last_fired.find(rule.name);
            if(fired != last_fired.end() && now - fired->second < rule.cooldown)
                continue;

            last_fired[rule.name] = now;
            Alert alert{rule.name, rule.metric_name, value, rule.threshold, std::chrono::system_clock::now()};
            for(const auto& notifier : notifiers)
                notifier->notify(alert);
        }
    }
}

AlertOperator parse_alert_operator(const std::string& value)
{
    if(value == ">=")
        return AlertOperator::GreaterOrEqual;
    if(value == "<")
        return AlertOperator::LessThan;
    if(value == "<=")
        return AlertOperator::LessOrEqual;
    if(value == "==" || value == "=")
        return AlertOperator::Equal;
    return AlertOperator::GreaterThan;
}

} // namespace telemetry
