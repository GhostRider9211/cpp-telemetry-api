#include "telemetry_store.hpp"

void TelemetryStore::add(const Telemetry& t)
{
    std::lock_guard<std::mutex> lock(mtx);
    data.push_back(t);
}

std::vector<Telemetry> TelemetryStore::get_all()
{
    std::lock_guard<std::mutex> lock(mtx);
    return data;
}

double TelemetryStore::avg_temperature()
{
    std::lock_guard<std::mutex> lock(mtx);

    if(data.empty())
        return 0;

    double sum = 0;

    for(const auto& t : data)
        sum += t.temperature;

    return sum / data.size();
}