# Telemetry API

A C++17 telemetry ingestion service for accepting IoT sensor readings, storing recent readings in memory, emitting structured telemetry events, and exposing runtime metrics.

The project combines a small REST API with a telemetry pipeline: incoming readings update metrics, generate structured events, flow through a bounded asynchronous queue, and can be exported to stdout, files, HTTP endpoints, TCP, or UDP sinks.

## Features

- REST API built with C++17 and `cpp-httplib`
- JSON request and response handling with `nlohmann/json`
- Thread-safe in-memory telemetry storage
- Average temperature and reading count statistics
- Structured event logging with correlation and request IDs
- Asynchronous telemetry pipeline with batching, sampling, rate limiting, and overflow policies
- Bounded per-sink queues with retry and exponential backoff
- Sink implementations for stdout, file, HTTP, TCP, and UDP
- Counters, gauges, histograms, timers, and summaries
- Prometheus-compatible metrics endpoint
- OTLP-ready JSON metrics endpoint
- Health endpoint with process diagnostics
- Server-sent event stream for live telemetry events
- Threshold alert engine with cooldowns and stdout notifications
- JSON configuration with environment variable overrides
- Config file watcher for runtime pipeline updates
- Unit tests for storage, metrics, pipeline behavior, serialization, and sink failures
- Optional `clang-tidy`, `cppcheck`, and coverage build support

## Tech Stack

- C++17
- CMake 3.16+
- `cpp-httplib` for the embedded HTTP server
- `nlohmann/json` for JSON parsing and serialization
- GitHub Actions for CI

Both third-party headers are vendored under `include/`, so no package manager is required for the default build.

## Project Layout

```text
telemetry-api/
|-- include/
|   |-- httplib.h
|   |-- nlohmann/json.hpp
|   |-- telemetry.hpp
|   |-- telemetry_alerting.hpp
|   |-- telemetry_config.hpp
|   |-- telemetry_diagnostics.hpp
|   |-- telemetry_event.hpp
|   |-- telemetry_exporter.hpp
|   |-- telemetry_metrics.hpp
|   |-- telemetry_monitoring.hpp
|   |-- telemetry_pipeline.hpp
|   |-- telemetry_queue.hpp
|   |-- telemetry_serializer.hpp
|   |-- telemetry_sinks.hpp
|   `-- telemetry_store.hpp
|-- src/
|   |-- main.cpp
|   `-- telemetry_*.cpp
|-- tests/
|   |-- metrics_test.cpp
|   |-- pipeline_test.cpp
|   |-- serialization_test.cpp
|   |-- sink_failure_test.cpp
|   `-- telemetry_test.cpp
|-- .github/workflows/
|-- CMakeLists.txt
`-- README.md
```

## Build

```bash
cmake -S . -B build
cmake --build build --parallel
```

On Windows with a multi-config generator, the executable is usually placed under a configuration directory such as `build/Debug/telemetry_api.exe` or `build/Release/telemetry_api.exe`.

## Run

Linux/macOS:

```bash
./build/telemetry_api
```

Windows PowerShell:

```powershell
.\build\Debug\telemetry_api.exe
```

By default, the server listens on:

```text
http://0.0.0.0:8080
```

Use `TELEMETRY_PORT` or a JSON config file to change the port.

## Deploy on Render

The repository includes a multi-stage Dockerfile and a render.yaml Blueprint.

1. Push the repository to GitHub.
2. In Render, create a new Blueprint and connect the repository.
3. Render builds the Docker image and checks /health before making the service live.

The server reads Render's PORT environment variable automatically. TELEMETRY_PORT remains available as an explicit override.

After deployment, verify https://your-service.onrender.com/health.

## API

### `POST /telemetry`

Accepts a sensor reading, stores it in memory, updates sensor metrics, and emits a structured event into the telemetry pipeline.

```bash
curl -X POST http://localhost:8080/telemetry \
  -H "Content-Type: application/json" \
  -H "X-Correlation-Id: demo-correlation-id" \
  -H "X-Request-Id: demo-request-id" \
  -d '{"sensor_id":"sensor_1","temperature":25.5,"humidity":60,"timestamp":1710000000}'
```

Successful response:

```text
Telemetry added
```

Invalid JSON or missing fields return `400`:

```json
{"error":"invalid telemetry payload"}
```

### `GET /telemetry`

Returns all telemetry records currently stored in memory.

```bash
curl http://localhost:8080/telemetry
```

```json
[
  {
    "sensor_id": "sensor_1",
    "temperature": 25.5,
    "humidity": 60,
    "timestamp": 1710000000
  }
]
```

### `GET /stats`

Returns aggregate statistics for stored readings.

```bash
curl http://localhost:8080/stats
```

```json
{"avg_temperature":25.5,"count":1}
```

### `POST /flush`

Flushes the telemetry pipeline.

```bash
curl -X POST http://localhost:8080/flush
```

```text
flushed
```

### `GET /health`

Returns process health and lightweight runtime diagnostics.

```bash
curl http://localhost:8080/health
```

Example fields include `status`, `process_id`, `host`, `thread_id`, and `uptime_ms`.

### `GET /metrics`

Returns a Prometheus-compatible metrics scrape.

```bash
curl http://localhost:8080/metrics
```

Metrics include HTTP request timing, active requests, pipeline counters, sink drops, and sensor reading metrics.

### `GET /otlp/v1/metrics`

Returns metrics as schema-tagged JSON through the OTLP-ready exporter abstraction.

```bash
curl http://localhost:8080/otlp/v1/metrics
```

### `GET /telemetry/live`

Streams accepted telemetry events as server-sent events.

```bash
curl -N http://localhost:8080/telemetry/live
```

The stream sends heartbeat comments when no events are available.

## Configuration

The server can run with defaults, environment variables, or a JSON config file.

Set `TELEMETRY_CONFIG` to load a JSON configuration file:

```powershell
$env:TELEMETRY_CONFIG = "telemetry.config.json"
.\build\Debug\telemetry_api.exe
```

Example config:

```json
{
  "monitoring": {
    "host": "0.0.0.0",
    "port": 8080
  },
  "pipeline": {
    "queue_size": 8192,
    "batch_size": 128,
    "worker_count": 2,
    "flush_interval_ms": 1000,
    "overflow_policy": "drop_newest",
    "sampling_rate": 1.0,
    "rate_limit_per_second": 0
  },
  "sinks": [
    {
      "type": "stdout",
      "name": "console"
    },
    {
      "type": "file",
      "name": "events-file",
      "target": "telemetry.log",
      "queue_size": 4096,
      "overflow_policy": "drop_oldest",
      "retry": {
        "max_attempts": 3,
        "initial_backoff_ms": 50,
        "max_backoff_ms": 2000
      }
    }
  ],
  "alerts": [
    {
      "name": "high_temperature",
      "metric": "sensor_temperature_celsius",
      "op": ">",
      "threshold": 80,
      "cooldown_ms": 30000
    }
  ]
}
```

Supported sink `type` values:

- `stdout`
- `file`
- `http`
- `tcp`
- `udp`

For `http`, `tcp`, and `udp` sinks, set `target` to the destination URL or host/port, for example:

```json
{"type":"http","name":"collector","target":"http://localhost:4318/events"}
```

```json
{"type":"udp","name":"udp-collector","target":"udp://localhost:9000"}
```

Supported overflow policies:

- `drop_newest`
- `drop_oldest`
- `block`

Supported alert operators:

- `>`
- `>=`
- `<`
- `<=`
- `==`

Environment variables override file values for selected runtime settings:

| Variable | Description |
| --- | --- |
| `TELEMETRY_CONFIG` | Path to a JSON config file |
| `TELEMETRY_PORT` | HTTP server port |
| `TELEMETRY_QUEUE_SIZE` | Main pipeline queue size |
| `TELEMETRY_BATCH_SIZE` | Pipeline batch size |
| `TELEMETRY_WORKERS` | Number of pipeline worker threads |
| `TELEMETRY_SAMPLING_RATE` | Event sampling rate from `0.0` to `1.0` |

When `TELEMETRY_CONFIG` is set, the file is watched and pipeline settings are reloaded while the process is running. Sinks and alert rules are loaded during startup.

## Tests

Build and run the test suite:

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The configured tests are:

- `telemetry_test`
- `metrics_test`
- `pipeline_test`
- `serialization_test`
- `sink_failure_test`

## Tooling

Enable `clang-tidy` during compilation:

```bash
cmake -S . -B build -DTELEMETRY_ENABLE_CLANG_TIDY=ON
cmake --build build --parallel
```

Run `cppcheck` if it is installed:

```bash
cmake --build build --target cppcheck
```

Build with coverage instrumentation on GCC or Clang:

```bash
cmake -S . -B build-coverage -DTELEMETRY_ENABLE_COVERAGE=ON
cmake --build build-coverage --parallel
ctest --test-dir build-coverage --output-on-failure
```

## CI

GitHub Actions builds the project, runs tests, executes `cppcheck`, and validates a coverage build on Ubuntu.

## License

This project is for educational purposes and demonstration of modern C++ backend and telemetry service design.
