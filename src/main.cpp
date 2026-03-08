#include "httplib.h"
#include "telemetry_store.hpp"
#include "telemetry.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
using json = nlohmann::json;

TelemetryStore store;

int main()
{
    httplib::Server svr;

    // POST /telemetry
    svr.Post("/telemetry", [](const httplib::Request& req, httplib::Response& res)
    {
        auto body = json::parse(req.body);

        Telemetry t;

        t.sensor_id = body["sensor_id"];
        t.temperature = body["temperature"];
        t.humidity = body["humidity"];
        t.timestamp = body["timestamp"];

        store.add(t);

        res.set_content("Telemetry added", "text/plain");
    });

    // GET /stats
    svr.Get("/stats", [](const httplib::Request&, httplib::Response& res)
    {
        json response;

        response["avg_temperature"] = store.avg_temperature();

        res.set_content(response.dump(), "application/json");
    });
    svr.Get("/telemetry", [](const httplib::Request&, httplib::Response& res)
{
    auto data = store.get_all();

    json response = json::array();

    for(const auto& t : data)
    {
        response.push_back({
            {"sensor_id", t.sensor_id},
            {"temperature", t.temperature},
            {"humidity", t.humidity},
            {"timestamp", t.timestamp}
        });
    }

    res.set_content(response.dump(), "application/json");
});

    std::cout << "Server running on port 8080\n";

    svr.listen("0.0.0.0", 8080);
}