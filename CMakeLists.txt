cmake_minimum_required(VERSION 3.16)

project(telemetry_api LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TELEMETRY_ENABLE_COVERAGE "Build with coverage instrumentation" OFF)
option(TELEMETRY_ENABLE_CLANG_TIDY "Run clang-tidy during compilation" OFF)

if(TELEMETRY_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    endif()
endif()

if(TELEMETRY_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

set(TELEMETRY_CORE_SOURCES
    src/telemetry_alerting.cpp
    src/telemetry_config.cpp
    src/telemetry_diagnostics.cpp
    src/telemetry_event.cpp
    src/telemetry_exporter.cpp
    src/telemetry_metrics.cpp
    src/telemetry_monitoring.cpp
    src/telemetry_pipeline.cpp
    src/telemetry_serializer.cpp
    src/telemetry_sinks.cpp
    src/telemetry_store.cpp
)

add_library(telemetry_core ${TELEMETRY_CORE_SOURCES})
target_include_directories(telemetry_core PUBLIC include)

if(WIN32)
    target_compile_definitions(telemetry_core PUBLIC _WIN32_WINNT=0x0A00 NOMINMAX)
    target_link_libraries(telemetry_core PUBLIC ws2_32)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(telemetry_core PUBLIC stdc++fs)
endif()

add_executable(telemetry_api src/main.cpp)
target_link_libraries(telemetry_api PRIVATE telemetry_core)

enable_testing()

function(add_telemetry_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE telemetry_core)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_telemetry_test(telemetry_test tests/telemetry_test.cpp)
add_telemetry_test(config_test tests/config_test.cpp)
add_telemetry_test(metrics_test tests/metrics_test.cpp)
add_telemetry_test(pipeline_test tests/pipeline_test.cpp)
add_telemetry_test(serialization_test tests/serialization_test.cpp)
add_telemetry_test(sink_failure_test tests/sink_failure_test.cpp)

find_program(CPPCHECK_EXE NAMES cppcheck)
if(CPPCHECK_EXE)
    add_custom_target(cppcheck
        COMMAND ${CPPCHECK_EXE}
            --enable=warning,performance,portability
            --std=c++17
            --inline-suppr
            --suppress=missingIncludeSystem
            -I ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/tests
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
