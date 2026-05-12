#pragma once

#include "telemetry.hpp"
#include <cstddef>
#include <vector>
#include <mutex>

class TelemetryStore
{
private:
    std::vector<Telemetry> data;
    mutable std::mutex mtx;

public:
    void add(const Telemetry& t);
    std::vector<Telemetry> get_all() const;
    double avg_temperature() const;
    std::size_t size() const;
};
