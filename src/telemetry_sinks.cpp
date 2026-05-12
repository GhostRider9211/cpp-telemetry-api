#include "telemetry_sinks.hpp"

#include "httplib.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace telemetry
{

namespace
{

#ifdef _WIN32
struct WinsockSession
{
    WinsockSession()
    {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
    }

    ~WinsockSession()
    {
        WSACleanup();
    }
};
#endif

void close_socket_platform(int socket_fd)
{
#ifdef _WIN32
    closesocket(static_cast<SOCKET>(socket_fd));
#else
    close(socket_fd);
#endif
}

struct ParsedUrl
{
    std::string host;
    int port{80};
    std::string path{"/"};
};

ParsedUrl parse_url(const std::string& url)
{
    std::string value = url;
    const std::string http_prefix = "http://";
    if(value.rfind(http_prefix, 0) == 0)
        value.erase(0, http_prefix.size());
    if(value.rfind("tcp://", 0) == 0 || value.rfind("udp://", 0) == 0)
        value.erase(0, 6);

    ParsedUrl parsed;
    const auto slash = value.find('/');
    const auto host_port = slash == std::string::npos ? value : value.substr(0, slash);
    parsed.path = slash == std::string::npos ? "/" : value.substr(slash);

    const auto colon = host_port.find(':');
    if(colon == std::string::npos)
    {
        parsed.host = host_port;
    }
    else
    {
        parsed.host = host_port.substr(0, colon);
        parsed.port = std::stoi(host_port.substr(colon + 1));
    }

    return parsed;
}

bool send_socket_payload(const std::string& host, int port, SocketProtocol protocol, const SerializedPayload& payload)
{
#ifdef _WIN32
    static WinsockSession winsock;
#endif

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = protocol == SocketProtocol::Udp ? SOCK_DGRAM : SOCK_STREAM;

    addrinfo* result = nullptr;
    const auto port_text = std::to_string(port);
    if(getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result) != 0)
        return false;

    bool sent = false;
    for(addrinfo* item = result; item != nullptr && !sent; item = item->ai_next)
    {
        const auto fd = static_cast<int>(socket(item->ai_family, item->ai_socktype, item->ai_protocol));
        if(fd < 0)
            continue;

        if(protocol == SocketProtocol::Tcp && connect(fd, item->ai_addr, static_cast<int>(item->ai_addrlen)) != 0)
        {
            close_socket_platform(fd);
            continue;
        }

        const char* data = reinterpret_cast<const char*>(payload.bytes.data());
        const auto length = static_cast<int>(payload.bytes.size());
        const int written = protocol == SocketProtocol::Udp
            ? sendto(fd, data, length, 0, item->ai_addr, static_cast<int>(item->ai_addrlen))
            : send(fd, data, length, 0);

        sent = written == length;
        close_socket_platform(fd);
    }

    freeaddrinfo(result);
    return sent;
}

} // namespace

StdoutSink::StdoutSink(std::ostream& stream, std::string name)
    : sink_name(std::move(name)),
      out(stream)
{
}

const std::string& StdoutSink::name() const
{
    return sink_name;
}

bool StdoutSink::write(const SerializedPayload& payload)
{
    std::lock_guard<std::mutex> lock(mtx);
    out.write(reinterpret_cast<const char*>(payload.bytes.data()), static_cast<std::streamsize>(payload.bytes.size()));
    out << '\n';
    return static_cast<bool>(out);
}

void StdoutSink::flush()
{
    std::lock_guard<std::mutex> lock(mtx);
    out.flush();
}

FileSink::FileSink(std::string path, std::string name)
    : sink_name(std::move(name)),
      file(path, std::ios::app | std::ios::binary)
{
    if(!file)
        throw std::runtime_error("unable to open telemetry file sink: " + path);
}

const std::string& FileSink::name() const
{
    return sink_name;
}

bool FileSink::write(const SerializedPayload& payload)
{
    std::lock_guard<std::mutex> lock(mtx);
    file.write(reinterpret_cast<const char*>(payload.bytes.data()), static_cast<std::streamsize>(payload.bytes.size()));
    file << '\n';
    return static_cast<bool>(file);
}

void FileSink::flush()
{
    std::lock_guard<std::mutex> lock(mtx);
    file.flush();
}

HttpSink::HttpSink(std::string host_name, int host_port, std::string endpoint, std::string name)
    : sink_name(std::move(name)),
      host(std::move(host_name)),
      port(host_port),
      path(std::move(endpoint))
{
}

HttpSink::HttpSink(std::string url, std::string name)
    : sink_name(std::move(name))
{
    const auto parsed = parse_url(url);
    host = parsed.host;
    port = parsed.port;
    path = parsed.path;
}

const std::string& HttpSink::name() const
{
    return sink_name;
}

bool HttpSink::write(const SerializedPayload& payload)
{
    httplib::Client client(host, port);
    const auto body = payload_to_string(payload);
    auto result = client.Post(path.c_str(), body, payload.content_type.c_str());
    return result && result->status >= 200 && result->status < 300;
}

SocketSink::SocketSink(std::string host_name, int host_port, SocketProtocol socket_protocol, std::string name)
    : sink_name(std::move(name)),
      host(std::move(host_name)),
      port(host_port),
      protocol(socket_protocol)
{
}

const std::string& SocketSink::name() const
{
    return sink_name;
}

bool SocketSink::write(const SerializedPayload& payload)
{
    return send_socket_payload(host, port, protocol, payload);
}

SinkWorker::SinkWorker(std::shared_ptr<Sink> output_sink, std::size_t queue_size, OverflowPolicy policy, RetryConfig retry_config)
    : sink(std::move(output_sink)),
      queue(queue_size),
      overflow_policy(policy),
      retry(retry_config)
{
}

SinkWorker::~SinkWorker()
{
    stop();
}

void SinkWorker::start()
{
    if(running.exchange(true))
        return;

    worker = std::thread([this] {
        std::shared_ptr<const SerializedPayload> payload;
        while(queue.pop(payload))
        {
            if(payload && !write_with_retry(*payload))
                write_failures.fetch_add(1, std::memory_order_relaxed);
        }

        flush();
    });
}

void SinkWorker::stop()
{
    if(!running.exchange(false))
        return;

    queue.close();
    if(worker.joinable())
        worker.join();
}

bool SinkWorker::submit(std::shared_ptr<const SerializedPayload> payload)
{
    std::size_t dropped_now = 0;
    const bool accepted = queue.try_push(std::move(payload), overflow_policy, &dropped_now);
    dropped.fetch_add(dropped_now, std::memory_order_relaxed);
    if(accepted)
        enqueued.fetch_add(1, std::memory_order_relaxed);
    return accepted;
}

void SinkWorker::flush()
{
    if(sink)
        sink->flush();
}

std::string SinkWorker::name() const
{
    return sink ? sink->name() : "";
}

SinkWorkerStats SinkWorker::stats() const
{
    return SinkWorkerStats{
        enqueued.load(std::memory_order_relaxed),
        dropped.load(std::memory_order_relaxed),
        write_failures.load(std::memory_order_relaxed)};
}

bool SinkWorker::write_with_retry(const SerializedPayload& payload)
{
    auto delay = retry.initial_backoff;
    const int attempts = std::max(1, retry.max_attempts);

    for(int attempt = 1; attempt <= attempts; ++attempt)
    {
        if(sink->write(payload))
            return true;

        if(attempt < attempts)
        {
            std::this_thread::sleep_for(delay);
            delay = std::min(delay * 2, retry.max_backoff);
        }
    }

    return false;
}

std::shared_ptr<Sink> make_sink(const SinkConfig& config)
{
    if(config.type == "stdout")
        return std::make_shared<StdoutSink>(std::cout, config.name);

    if(config.type == "file")
        return std::make_shared<FileSink>(config.target, config.name);

    if(config.type == "http")
        return std::make_shared<HttpSink>(config.target, config.name);

    if(config.type == "tcp")
    {
        const auto parsed = parse_url(config.target);
        return std::make_shared<SocketSink>(parsed.host, parsed.port, SocketProtocol::Tcp, config.name);
    }

    if(config.type == "udp")
    {
        const auto parsed = parse_url(config.target);
        return std::make_shared<SocketSink>(parsed.host, parsed.port, SocketProtocol::Udp, config.name);
    }

    throw std::invalid_argument("unknown telemetry sink type: " + config.type);
}

} // namespace telemetry
