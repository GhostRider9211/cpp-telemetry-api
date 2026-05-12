#include "telemetry_sinks.hpp"

#include <cassert>
#include <memory>

class FailingSink final : public telemetry::Sink
{
private:
    std::string sink_name{"failing"};

public:
    const std::string& name() const override
    {
        return sink_name;
    }

    bool write(const telemetry::SerializedPayload&) override
    {
        return false;
    }
};

int main()
{
    telemetry::RetryConfig retry;
    retry.max_attempts = 1;

    telemetry::SinkWorker worker(std::make_shared<FailingSink>(), 4, telemetry::OverflowPolicy::DropNewest, retry);
    worker.start();

    telemetry::SerializedPayload payload;
    payload.bytes = {'x'};
    assert(worker.submit(std::make_shared<telemetry::SerializedPayload>(payload)));
    worker.stop();

    const auto stats = worker.stats();
    assert(stats.enqueued == 1);
    assert(stats.write_failures == 1);
    return 0;
}
