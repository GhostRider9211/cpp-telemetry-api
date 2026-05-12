#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace telemetry
{

using Labels = std::vector<std::pair<std::string, std::string>>;

enum class MetricType
{
    Counter,
    Gauge,
    Histogram,
    Timer,
    Summary
};

struct MetricOptions
{
    std::string description;
    std::string unit;
    std::chrono::milliseconds retention{std::chrono::minutes(5)};
    std::vector<double> histogram_buckets{0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0};
};

struct MetricSnapshot
{
    MetricType type{MetricType::Counter};
    std::string name;
    std::string description;
    std::string unit;
    Labels labels;
    double value{0.0};
    std::uint64_t count{0};
    double sum{0.0};
    std::vector<std::pair<double, std::uint64_t>> buckets;
    std::vector<std::pair<double, double>> quantiles;
};

class Metric
{
private:
    std::string metric_name;
    std::string metric_description;
    std::string metric_unit;
    Labels metric_labels;

public:
    Metric(std::string name, MetricOptions options, Labels labels);
    virtual ~Metric() = default;

    const std::string& name() const;
    const std::string& description() const;
    const std::string& unit() const;
    const Labels& labels() const;

    virtual MetricType type() const = 0;
    virtual MetricSnapshot snapshot() const = 0;
};

class Counter final : public Metric
{
private:
    std::atomic<std::uint64_t> value_bits{0};

public:
    Counter(std::string name, MetricOptions options = {}, Labels labels = {});

    void increment(std::uint64_t amount = 1);
    std::uint64_t value() const;

    MetricType type() const override;
    MetricSnapshot snapshot() const override;
};

class Gauge final : public Metric
{
private:
    std::atomic<double> current_value{0.0};

public:
    Gauge(std::string name, MetricOptions options = {}, Labels labels = {});

    void set(double value);
    void increment(double amount);
    void decrement(double amount);
    double value() const;

    MetricType type() const override;
    MetricSnapshot snapshot() const override;
};

class Histogram final : public Metric
{
private:
    std::vector<double> boundaries;
    std::vector<std::atomic<std::uint64_t>> bucket_counts;
    std::atomic<std::uint64_t> sample_count{0};
    mutable std::mutex sum_mtx;
    double sample_sum{0.0};

public:
    Histogram(std::string name, MetricOptions options = {}, Labels labels = {});

    void observe(double value);
    MetricType type() const override;
    MetricSnapshot snapshot() const override;
};

class Summary final : public Metric
{
private:
    struct Sample
    {
        double value;
        std::chrono::steady_clock::time_point timestamp;
    };

    std::chrono::milliseconds retention;
    mutable std::mutex mtx;
    mutable std::vector<Sample> samples;
    mutable double sample_sum{0.0};
    mutable std::uint64_t sample_count{0};

    void evict_locked(std::chrono::steady_clock::time_point now) const;

public:
    Summary(std::string name, MetricOptions options = {}, Labels labels = {});

    void observe(double value);
    MetricType type() const override;
    MetricSnapshot snapshot() const override;
};

class Timer final
{
private:
    Summary& metric;
    std::chrono::steady_clock::time_point started_at;
    bool stopped{false};

public:
    explicit Timer(Summary& summary);
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&& other) noexcept;
    Timer& operator=(Timer&& other) noexcept = delete;
    ~Timer();

    double stop();
};

class MetricRegistry
{
private:
    mutable std::mutex mtx;
    std::unordered_map<std::string, std::shared_ptr<Metric>> metrics;

    static std::string key_for(const std::string& name, const Labels& labels);

    template <typename T>
    std::shared_ptr<T> get_or_create(const std::string& name, MetricOptions options, Labels labels)
    {
        const auto key = key_for(name, labels);
        std::lock_guard<std::mutex> lock(mtx);

        auto found = metrics.find(key);
        if(found != metrics.end())
        {
            auto typed = std::dynamic_pointer_cast<T>(found->second);
            if(!typed)
                throw std::logic_error("metric registered with a different type: " + name);
            return typed;
        }

        auto metric = std::make_shared<T>(name, std::move(options), std::move(labels));
        metrics.emplace(key, metric);
        return metric;
    }

public:
    std::shared_ptr<Counter> counter(const std::string& name, MetricOptions options = {}, Labels labels = {});
    std::shared_ptr<Gauge> gauge(const std::string& name, MetricOptions options = {}, Labels labels = {});
    std::shared_ptr<Histogram> histogram(const std::string& name, MetricOptions options = {}, Labels labels = {});
    std::shared_ptr<Summary> summary(const std::string& name, MetricOptions options = {}, Labels labels = {});

    std::vector<MetricSnapshot> snapshot() const;
};

std::string metric_type_name(MetricType type);
std::string labels_to_prometheus(const Labels& labels);
std::string snapshots_to_prometheus(const std::vector<MetricSnapshot>& snapshots);

} // namespace telemetry
