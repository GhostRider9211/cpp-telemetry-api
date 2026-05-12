# Telemetry API (C++)

A lightweight REST API written in **C++** that collects telemetry data from IoT sensors and provides basic statistics.

The server accepts sensor readings such as temperature and humidity, stores them in memory, and exposes endpoints to retrieve statistics and stored data.

This project demonstrates building a **backend service in modern C++** using an embedded HTTP server and JSON processing.

---

## Features

- REST API server written in **C++17**
- Accept telemetry data from sensors
- Thread-safe in-memory storage
- Compute average temperature
- Retrieve stored telemetry data
- JSON request/response support
- Strongly typed counters, gauges, histograms, timers, and summaries
- Async structured event pipeline with batching, sampling, rate limiting, and bounded queues
- Pluggable stdout, file, HTTP, TCP, and UDP sinks with retry/backoff isolation
- JSON serializer with sink-independent payload contracts
- Prometheus scrape endpoint and OTLP-ready JSON metrics export endpoint
- Health endpoint, runtime diagnostics, and server-sent live telemetry stream
- Threshold alert engine with cooldowns and notifier abstraction
- Hot-reloadable typed configuration with environment overrides
- Unit, concurrency, serialization, and sink failure tests
- clang-tidy, cppcheck, CI, and coverage build support

---

## Tech Stack

- **C++17**
- **cpp-httplib** – lightweight HTTP server
- **nlohmann/json** – JSON parsing
- **CMake** – build system

---

## Project Structure

```
telemetry-api
│
├── include
│   ├── httplib.h
│   ├── telemetry.hpp
│   ├── telemetry_store.hpp
│   └── nlohmann
│       └── json.hpp
│
├── src
│   ├── main.cpp
│   └── telemetry_store.cpp
│
├── tests
│   └── telemetry_test.cpp
│
├── CMakeLists.txt
├── README.md
└── .gitignore
```

---

## Build Instructions

Clone the repository and build the project.

```
git clone https://github.com/GhostRider9211/cpp-telemetry-api.git
cd cpp-telemetry-api
mkdir build
cd build
cmake ..
make
```

---

## Run the Server

```
./telemetry_api
```

Server will start on:

```
http://localhost:8080
```

---

## API Endpoints

### POST /telemetry

Adds telemetry data from a sensor.

Example request:

```
curl -X POST http://localhost:8080/telemetry \
-H "Content-Type: application/json" \
-d '{"sensor_id":"sensor_1","temperature":25.5,"humidity":60,"timestamp":1710000000}'
```

Response:

```
Telemetry added
```

---

### GET /stats

Returns the average temperature of stored telemetry data.

Example:

```
curl http://localhost:8080/stats
```

Example response:

```
{"avg_temperature":25.5}
```

---

### GET /telemetry

Returns all stored telemetry records.

Example:

```
curl http://localhost:8080/telemetry
```

Example response:

```
[
  {
    "sensor_id":"sensor_1",
    "temperature":25.5,
    "humidity":60,
    "timestamp":1710000000
  }
]
```

---

### GET /health

Returns process health and lightweight runtime diagnostics.

```
curl http://localhost:8080/health
```

---

### GET /metrics

Returns Prometheus-compatible metrics.

```
curl http://localhost:8080/metrics
```

---

### GET /otlp/v1/metrics

Returns schema-tagged JSON metrics through the OTLP-ready exporter abstraction.

```
curl http://localhost:8080/otlp/v1/metrics
```

---

### GET /telemetry/live

Streams accepted telemetry events as server-sent events.

```
curl -N http://localhost:8080/telemetry/live
```

---

## Configuration

Set `TELEMETRY_CONFIG` to a JSON config file. Environment variables such as `TELEMETRY_PORT`, `TELEMETRY_QUEUE_SIZE`, `TELEMETRY_BATCH_SIZE`, `TELEMETRY_WORKERS`, and `TELEMETRY_SAMPLING_RATE` override file values.

Example:

```json
{
  "pipeline": {
    "queue_size": 8192,
    "batch_size": 128,
    "worker_count": 2,
    "flush_interval_ms": 1000,
    "overflow_policy": "drop_newest",
    "sampling_rate": 1.0
  },
  "sinks": [
    {"type": "stdout", "name": "stdout"},
    {"type": "file", "name": "events", "target": "telemetry.log"}
  ],
  "alerts": [
    {"name": "high_temperature", "metric": "sensor_temperature_celsius", "op": ">", "threshold": 80, "cooldown_ms": 30000}
  ]
}
```

---

## Running Tests

From the build directory:

```
ctest --output-on-failure
```

---

## Tooling

```
cmake -S . -B build -DTELEMETRY_ENABLE_CLANG_TIDY=ON
cmake --build build
cmake --build build --target cppcheck
cmake -S . -B build-coverage -DTELEMETRY_ENABLE_COVERAGE=ON
```

---

## License

This project is for educational purposes and demonstration of C++ backend development.
