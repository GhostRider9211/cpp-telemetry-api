#pragma once
#include <string>

struct Telemetry
{
    std::string sensor_id;
    double temperature;
    double humidity;
    long timestamp;
};