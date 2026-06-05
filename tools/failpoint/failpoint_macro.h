
#pragma once

#ifdef ENABLE_FAILPOINT

#include <chrono>
#include <thread>
#include "failpoint.h"

#define FAIL_POINT(name, action)                                \
    do {                                                        \
        if (failpoint::Registry::Instance().ShouldFire(name)) { \
            action;                                             \
        }                                                       \
    } while (0)

#define FAIL_POINT_RETURN(name, expr) FAIL_POINT(name, return expr)

#define FAIL_POINT_SLEEP(name, ms) \
    FAIL_POINT(name, std::this_thread::sleep_for(std::chrono::milliseconds(ms)))

#define FAIL_POINT_CRASH(name) FAIL_POINT(name, abort())

#else

#define FAIL_POINT(name, action) \
    do {                         \
    } while (0)
#define FAIL_POINT_RETURN(name, expr) \
    do {                              \
    } while (0)
#define FAIL_POINT_SLEEP(name, ms) \
    do {                           \
    } while (0)
#define FAIL_POINT_CRASH(name) \
    do {                       \
    } while (0)

#endif
