#pragma once

#include "telemetry_config.hpp"
#include "telemetry_queue.hpp"
#include "telemetry_serializer.hpp"

#include <atomic>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>

namespace telemetry
{

class Sink
{
public:
    virtual ~Sink() = default;

    virtual const std::string& name() const = 0;
    virtual bool write(const SerializedPayload& payload) = 0;
    virtual void flush() {}
};

class StdoutSink final : public Sink
{
private:
    std::string sink_name;
    std::ostream& out;
    std::mutex mtx;

public:
    explicit StdoutSink(std::ostream& stream = std::cout, std::string name = "stdout");

    const std::string& name() const override;
    bool write(const SerializedPayload& payload) override;
    void flush() override;
};

class FileSink final : public Sink
{
private:
    std::string sink_name;
    std::ofstream file;
    std::mutex mtx;

public:
    FileSink(std::string path, std::string name = "file");

    const std::string& name() const override;
    bool write(const SerializedPayload& payload) override;
    void flush() override;
};

class HttpSink final : public Sink
{
private:
    std::string sink_name;
    std::string host;
    int port{80};
    std::string path{"/"};

public:
    HttpSink(std::string host_name, int host_port, std::string endpoint, std::string name = "http");
    explicit HttpSink(std::string url, std::string name = "http");

    const std::string& name() const override;
    bool write(const SerializedPayload& payload) override;
};

enum class SocketProtocol
{
    Tcp,
    Udp
};

class SocketSink final : public Sink
{
private:
    std::string sink_name;
    std::string host;
    int port{0};
    SocketProtocol protocol{SocketProtocol::Udp};

public:
    SocketSink(std::string host_name, int host_port, SocketProtocol socket_protocol, std::string name = "socket");

    const std::string& name() const override;
    bool write(const SerializedPayload& payload) override;
};

struct SinkWorkerStats
{
    std::uint64_t enqueued{0};
    std::uint64_t dropped{0};
    std::uint64_t write_failures{0};
};

class SinkWorker
{
private:
    std::shared_ptr<Sink> sink;
    BoundedQueue<std::shared_ptr<const SerializedPayload>> queue;
    OverflowPolicy overflow_policy;
    RetryConfig retry;
    std::atomic<bool> running{false};
    std::thread worker;
    std::atomic<std::uint64_t> enqueued{0};
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<std::uint64_t> write_failures{0};

    bool write_with_retry(const SerializedPayload& payload);

public:
    SinkWorker(std::shared_ptr<Sink> output_sink, std::size_t queue_size, OverflowPolicy policy, RetryConfig retry_config);
    ~SinkWorker();

    SinkWorker(const SinkWorker&) = delete;
    SinkWorker& operator=(const SinkWorker&) = delete;

    void start();
    void stop();
    bool submit(std::shared_ptr<const SerializedPayload> payload);
    void flush();
    std::string name() const;
    SinkWorkerStats stats() const;
};

std::shared_ptr<Sink> make_sink(const SinkConfig& config);

} // namespace telemetry
