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
    set_environment("PORT", "9090");
    assert(telemetry::ConfigLoader::from_file("").monitoring.port == 9090);

    set_environment("TELEMETRY_PORT", "9191");
    assert(telemetry::ConfigLoader::from_file("").monitoring.port == 9191);

    clear_environment("TELEMETRY_PORT");
    clear_environment("PORT");
    return 0;
}
