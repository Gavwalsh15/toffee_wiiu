#pragma once
// A minimal test harness: CHECK records a failure and carries on, so one run shows every problem.
#include <cstdio>

namespace test {
inline int failures = 0;
}

#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition);     \
            ++test::failures;                                                                      \
        }                                                                                          \
    } while (0)
