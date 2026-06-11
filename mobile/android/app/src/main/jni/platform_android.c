/**
 * Android platform implementation.
 * Provides qmini_get_tick_count() using clock_gettime.
 */
#include <time.h>
#include <stdint.h>
#include "../../shared/qmini_platform.h"

uint32_t qmini_get_tick_count(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

uint64_t qmini_get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec;
}
