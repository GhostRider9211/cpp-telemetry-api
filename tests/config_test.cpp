#include "telemetry_config.hpp"

#include <cassert>
#include <cstdlib>

namespace
{

void set_environment(const char* name, const char* value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void clear_environment(const char* name)
{
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

} // namespace

int main()
{
    clear_environment("TELEMETRY_PORT");
    clear_environment("TELEMETRY_METRICS_TOKEN");
    set_environment("PORT", "9090");
    assert(telemetry::ConfigLoader::from_file("").monitoring.port == 9090);

    set_environment("TELEMETRY_PORT", "9191");
    assert(telemetry::ConfigLoader::from_file("").monitoring.port == 9191);

    set_environment("TELEMETRY_METRICS_TOKEN", "grafana-test-token");
    assert(telemetry::ConfigLoader::from_file("").monitoring.metrics_bearer_token == "grafana-test-token");

    clear_environment("TELEMETRY_PORT");
    clear_environment("TELEMETRY_METRICS_TOKEN");
    clear_environment("PORT");
    return 0;
}
