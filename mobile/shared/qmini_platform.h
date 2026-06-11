/**
 * Platform abstraction layer.
 * Each platform (Android/iOS) provides its own implementation of these functions.
 */
#ifndef QMINI_PLATFORM_H
#define QMINI_PLATFORM_H

#include <stdint.h>

/**
 * Returns monotonic time in milliseconds.
 * Used by jitter buffer and congestion control.
 * Android: clock_gettime(CLOCK_MONOTONIC)
 * iOS: mach_absolute_time()
 */
uint32_t qmini_get_tick_count(void);

/**
 * Get current UTC timestamp in seconds (for logging, etc).
 */
uint64_t qmini_get_time_sec(void);

#endif
