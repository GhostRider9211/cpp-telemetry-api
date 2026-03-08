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
- Simple unit test

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

## Running Tests

From the build directory:

```
./telemetry_test
```

If the program exits successfully, the test passed.

---

## Future Improvements

- Input validation
- Structured logging
- Persistent storage (PostgreSQL)
- Authentication
- Docker containerization
- Advanced telemetry analytics

---

## License

This project is for educational purposes and demonstration of C++ backend development.