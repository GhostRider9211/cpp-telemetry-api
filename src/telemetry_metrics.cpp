#include "telemetry_metrics.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace telemetry
{

namespace
{

std::string escape_label_value(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());

    for(char ch : value)
    {
        if(ch == '\\' || ch == '"')
            escaped.push_back('\\');
        escaped.push_back(ch);
    }

    return escaped;
}

MetricSnapshot base_snapshot(const Metric& metric)
{
    MetricSnapshot snapshot;
    snapshot.type = metric.type();
    snapshot.name = metric.name();
    snapshot.description = metric.description();
    snapshot.unit = metric.unit();
    snapshot.labels = metric.labels();
    return snapshot;
}

} // namespace

Metric::Metric(std::string name, MetricOptions options, Labels labels)
    : metric_name(std::move(name)),
      metric_description(std::move(options.description)),
      metric_unit(std::move(options.unit)),
      metric_labels(std::move(labels))
{
}

const std::string& Metric::name() const
{
    return metric_name;
}

const std::string& Metric::description() const
{
    return metric_description;
}

const std::string& Metric::unit() const
{
    return metric_unit;
}

const Labels& Metric::labels() const
{
    return metric_labels;
}

Counter::Counter(std::string name, MetricOptions options, Labels labels)
    : Metric(std::move(name), std::move(options), std::move(labels))
{
}

void Counter::increment(std::uint64_t amount)
{
    value_bits.fetch_add(amount, std::memory_order_relaxed);
}

std::uint64_t Counter::value() const
{
    return value_bits.load(std::memory_order_relaxed);
}

MetricType Counter::type() const
{
    return MetricType::Counter;
}

MetricSnapshot Counter::snapshot() const
{
    auto result = base_snapshot(*this);
    result.value = static_cast<double>(value());
    result.count = value();
    return result;
}

Gauge::Gauge(std::string name, MetricOptions options, Labels labels)
    : Metric(std::move(name), std::move(options), std::move(labels))
{
}

void Gauge::set(double value)
{
    current_value.store(value, std::memory_order_relaxed);
}

void Gauge::increment(double amount)
{
    double old_value = current_value.load(std::memory_order_relaxed);
    while(!current_value.compare_exchange_weak(old_value, old_value + amount, std::memory_order_relaxed))
    {
    }
}

void Gauge::decrement(double amount)
{
    increment(-amount);
}

double Gauge::value() const
{
    return current_value.load(std::memory_order_relaxed);
}

MetricType Gauge::type() const
{
    return MetricType::Gauge;
}

MetricSnapshot Gauge::snapshot() const
{
    auto result = base_snapshot(*this);
    result.value = value();
    return result;
}

Histogram::Histogram(std::string name, MetricOptions options, Labels labels)
    : Metric(std::move(name), options, std::move(labels)),
      boundaries(std::move(options.histogram_buckets)),
      bucket_counts(boundaries.size() + 1)
{
    std::sort(boundaries.begin(), boundaries.end());
}

void Histogram::observe(double value)
{
    const auto bucket = static_cast<std::size_t>(
        std::upper_bound(boundaries.begin(), boundaries.end(), value) - boundaries.begin());
    bucket_counts[bucket].fetch_add(1, std::memory_order_relaxed);
    sample_count.fetch_add(1, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(sum_mtx);
    sample_sum += value;
}

MetricType Histogram::type() const
{
    return MetricType::Histogram;
}

MetricSnapshot Histogram::snapshot() const
{
    auto result = base_snapshot(*this);
    result.count = sample_count.load(std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(sum_mtx);
        result.sum = sample_sum;
    }

    std::uint64_t cumulative = 0;
    for(std::size_t i = 0; i < bucket_counts.size(); ++i)
    {
        cumulative += bucket_counts[i].load(std::memory_order_relaxed);
        const double upper = i < boundaries.size() ? boundaries[i] : std::numeric_limits<double>::infinity();
        result.buckets.emplace_back(upper, cumulative);
    }

    return result;
}

Summary::Summary(std::string name, MetricOptions options, Labels labels)
    : Metric(std::move(name), options, std::move(labels)),
      retention(options.retention)
{
}

void Summary::evict_locked(std::chrono::steady_clock::time_point now) const
{
    const auto keep_after = now - retention;
    auto first_valid = std::lower_bound(samples.begin(), samples.end(), keep_after,
        [](const Sample& sample, const std::chrono::steady_clock::time_point& threshold) {
            return sample.timestamp < threshold;
        });

    for(auto it = samples.begin(); it != first_valid; ++it)
    {
        sample_sum -= it->value;
        --sample_count;
    }

    samples.erase(samples.begin(), first_valid);
}

void Summary::observe(double value)
{
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mtx);
    evict_locked(now);
    samples.push_back({value, now});
    sample_sum += value;
    ++sample_count;
}

MetricType Summary::type() const
{
    return MetricType::Summary;
}

MetricSnapshot Summary::snapshot() const
{
    std::vector<double> values;
    auto result = base_snapshot(*this);

    {
        std::lock_guard<std::mutex> lock(mtx);
        evict_locked(std::chrono::steady_clock::now());
        values.reserve(samples.size());
        for(const auto& sample : samples)
            values.push_back(sample.value);

        result.count = sample_count;
        result.sum = sample_sum;
    }

    if(values.empty())
        return result;

    std::sort(values.begin(), values.end());
    const auto quantile = [&values](double q) {
        const auto index = static_cast<std::size_t>(q * static_cast<double>(values.size() - 1));
        return values[index];
    };

    result.quantiles.emplace_back(0.5, quantile(0.5));
    result.quantiles.emplace_back(0.9, quantile(0.9));
    result.quantiles.emplace_back(0.99, quantile(0.99));
    result.value = result.count == 0 ? 0.0 : result.sum / static_cast<double>(result.count);
    return result;
}

Timer::Timer(Summary& summary)
    : metric(summary),
      started_at(std::chrono::steady_clock::now())
{
}

Timer::Timer(Timer&& other) noexcept
    : metric(other.metric),
      started_at(other.started_at),
      stopped(other.stopped)
{
    other.stopped = true;
}

Timer::~Timer()
{
    if(!stopped)
        stop();
}

double Timer::stop()
{
    if(stopped)
        return 0.0;

    stopped = true;
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_at).count();
    metric.observe(elapsed);
    return elapsed;
}

std::string MetricRegistry::key_for(const std::string& name, const Labels& labels)
{
    std::ostringstream out;
    out << name;
    for(const auto& label : labels)
        out << '\xff' << label.first << '=' << label.second;
    return out.str();
}

std::shared_ptr<Counter> MetricRegistry::counter(const std::string& name, MetricOptions options, Labels labels)
{
    return get_or_create<Counter>(name, std::move(options), std::move(labels));
}

std::shared_ptr<Gauge> MetricRegistry::gauge(const std::string& name, MetricOptions options, Labels labels)
{
    return get_or_create<Gauge>(name, std::move(options), std::move(labels));
}

std::shared_ptr<Histogram> MetricRegistry::histogram(const std::string& name, MetricOptions options, Labels labels)
{
    return get_or_create<Histogram>(name, std::move(options), std::move(labels));
}

std::shared_ptr<Summary> MetricRegistry::summary(const std::string& name, MetricOptions options, Labels labels)
{
    return get_or_create<Summary>(name, std::move(options), std::move(labels));
}

std::vector<MetricSnapshot> MetricRegistry::snapshot() const
{
    std::vector<std::shared_ptr<Metric>> copied;

    {
        std::lock_guard<std::mutex> lock(mtx);
        copied.reserve(metrics.size());
        for(const auto& metric : metrics)
            copied.push_back(metric.second);
    }

    std::vector<MetricSnapshot> result;
    result.reserve(copied.size());

    for(const auto& metric : copied)
        result.push_back(metric->snapshot());

    std::sort(result.begin(), result.end(), [](const MetricSnapshot& left, const MetricSnapshot& right) {
        return left.name < right.name;
    });

    return result;
}

std::string metric_type_name(MetricType type)
{
    switch(type)
    {
    case MetricType::Counter:
        return "counter";
    case MetricType::Gauge:
        return "gauge";
    case MetricType::Histogram:
        return "histogram";
    case MetricType::Timer:
        return "summary";
    case MetricType::Summary:
        return "summary";
    }

    return "gauge";
}

std::string labels_to_prometheus(const Labels& labels)
{
    if(labels.empty())
        return {};

    std::ostringstream out;
    out << '{';
    for(std::size_t i = 0; i < labels.size(); ++i)
    {
        if(i > 0)
            out << ',';
        out << labels[i].first << "=\"" << escape_label_value(labels[i].second) << '"';
    }
    out << '}';
    return out.str();
}

std::string snapshots_to_prometheus(const std::vector<MetricSnapshot>& snapshots)
{
    std::ostringstream out;
    out << std::setprecision(12);

    for(const auto& metric : snapshots)
    {
        if(!metric.description.empty())
            out << "# HELP " << metric.name << ' ' << metric.description << '\n';
        out << "# TYPE " << metric.name << ' ' << metric_type_name(metric.type) << '\n';

        if(metric.type == MetricType::Histogram)
        {
            for(const auto& bucket : metric.buckets)
            {
                Labels labels = metric.labels;
                labels.emplace_back("le", std::isinf(bucket.first) ? "+Inf" : std::to_string(bucket.first));
                out << metric.name << "_bucket" << labels_to_prometheus(labels) << ' ' << bucket.second << '\n';
            }
            out << metric.name << "_sum" << labels_to_prometheus(metric.labels) << ' ' << metric.sum << '\n';
            out << metric.name << "_count" << labels_to_prometheus(metric.labels) << ' ' << metric.count << '\n';
            continue;
        }

        if(metric.type == MetricType::Summary)
        {
            for(const auto& quantile : metric.quantiles)
            {
                Labels labels = metric.labels;
                labels.emplace_back("quantile", std::to_string(quantile.first));
                out << metric.name << labels_to_prometheus(labels) << ' ' << quantile.second << '\n';
            }
            out << metric.name << "_sum" << labels_to_prometheus(metric.labels) << ' ' << metric.sum << '\n';
            out << metric.name << "_count" << labels_to_prometheus(metric.labels) << ' ' << metric.count << '\n';
            continue;
        }

        out << metric.name << labels_to_prometheus(metric.labels) << ' ' << metric.value << '\n';
    }

    return out.str();
}

} // namespace telemetry
