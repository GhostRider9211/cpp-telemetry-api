#include "telemetry_store.hpp"
#include <cassert>

int main()
{
    TelemetryStore store;

    Telemetry t1{"sensor_1",25,60,1};
    Telemetry t2{"sensor_2",27,55,2};

    store.add(t1);
    store.add(t2);

    double avg = store.avg_temperature();

    assert(avg == 26);

    return 0;
}