#include "telemetry_store.hpp"

void TelemetryStore::add(const Telemetry& t)
{
    std::lock_guard<std::mutex> lock(mtx);
    data.push_back(t);
}

std::vector<Telemetry> TelemetryStore::get_all() const
{
    std::lock_guard<std::mutex> lock(mtx);
    return data;
}

double TelemetryStore::avg_temperature() const
{
    std::lock_guard<std::mutex> lock(mtx);

    if(data.empty())
        return 0;

    double sum = 0;

    for(const auto& t : data)
        sum += t.temperature;

    return sum / data.size();
}

std::size_t TelemetryStore::size() const
{
    std::lock_guard<std::mutex> lock(mtx);
    return data.size();
}
