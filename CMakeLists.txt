cmake_minimum_required(VERSION 3.16)

project(telemetry_api)

set(CMAKE_CXX_STANDARD 17)

include_directories(include)

add_executable(telemetry_api
    src/main.cpp
    src/telemetry_store.cpp
)

add_executable(telemetry_test
    tests/telemetry_test.cpp
    src/telemetry_store.cpp
)