#pragma once

#include "telemetry.hpp"
#include <vector>
#include <mutex>

class TelemetryStore
{
private:
    std::vector<Telemetry> data;
    std::mutex mtx;

public:
    void add(const Telemetry& t);
    std::vector<Telemetry> get_all();
    double avg_temperature();
};