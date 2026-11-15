#include "logger/toffee_log.h"

#include <cstdio>

namespace logger {
void info(const char* message)
{
#if VERBOSE_LOGGING
    std::fprintf(stderr, "[toffee] %s\n", message);
#else
    (void)message;
#endif
}

void error(const char* message) { std::fprintf(stderr, "[toffee] error: %s\n", message); }
} // namespace logger
