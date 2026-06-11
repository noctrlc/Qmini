/**
 * iOS platform implementation.
 * Uses mach_absolute_time for monotonic clock.
 */
#include <mach/mach_time.h>
#include <stdint.h>
#include <time.h>

uint32_t qmini_get_tick_count(void) {
    static mach_timebase_info_data_t info = {0};
    if (info.denom == 0)
        mach_timebase_info(&info);
    uint64_t nanos = mach_absolute_time() * info.numer / info.denom;
    return (uint32_t)(nanos / 1000000);
}

uint64_t qmini_get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec;
}
