# Telemetry API (C++)

A simple REST API written in C++ that collects telemetry data from IoT sensors and provides basic statistics.

The server accepts sensor readings such as temperature and humidity, stores them in memory, and exposes endpoints to retrieve statistics and stored data.

---

## Features

- REST API server written in C++
- Accept telemetry data from sensors
- Thread-safe in-memory storage
- Compute average temperature
- Retrieve stored telemetry data
- JSON request/response support
- Unit test for telemetry storage

---

## Tech Stack

- C++17
- cpp-httplib (HTTP server)
- nlohmann/json (JSON parsing)
- CMake (build system)

---

## Project Structure

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

---

## Build Instructions

Clone the repository and build the project.

git clone https://github.com/YOUR_USERNAME/telemetry-api.git
cd telemetry-api
mkdir build
cd build
cmake ..
make

---

## Run the Server

./telemetry_api

Server will start on:

http://localhost:8080

---

## API Endpoints

### POST /telemetry

Adds telemetry data from a sensor.

Example request:

curl -X POST http://localhost:8080/telemetry \
-H "Content-Type: application/json" \
-d '{"sensor_id":"sensor_1","temperature":25.5,"humidity":60,"timestamp":1710000000}'

Response:

Telemetry added

---

### GET /stats

Returns the average temperature of stored telemetry data.

Example:

curl http://localhost:8080/stats

Response:

{"avg_temperature":25.5}

---

### GET /telemetry

Returns all stored telemetry records.

Example:

curl http://localhost:8080/telemetry

Response:

[
  {
    "sensor_id":"sensor_1",
    "temperature":25.5,
    "humidity":60,
    "timestamp":1710000000
  }
]

---

## Running Tests

From the build directory:

./telemetry_test

If the program exits successfully, the test passed.

---

## Future Improvements

- Input validation
- Logging support
- Persistent storage (PostgreSQL)
- Authentication
- Docker containerization
- More telemetry analytics