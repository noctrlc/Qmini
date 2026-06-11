/**
 * Adaptive jitter buffer for VoIP.
 * Portable version - no Win32 dependencies.
 */
#ifndef JITTER_BUFFER_H
#define JITTER_BUFFER_H

#include <stdint.h>

#define JB_CAPACITY      128   /* power of 2 */
#define JB_MAX_PACKET    400   /* max Opus encoded frame size */

typedef struct {
    uint8_t  data[JB_CAPACITY][JB_MAX_PACKET];
    int      sizes[JB_CAPACITY];
    uint16_t seq_numbers[JB_CAPACITY];
    int      count;
    int      read_cursor;
    int      target_level;
    uint32_t last_arrival_time;
    uint32_t jitter_avg;
    uint32_t jitter_variance;
    uint32_t last_adaptation;
    int      min_target;
    int      max_target;
} jitter_buffer_t;

void jitter_buffer_init(jitter_buffer_t *jb);
void jitter_buffer_push(jitter_buffer_t *jb, const uint8_t *data, int size, uint16_t seq);
int  jitter_buffer_pop(jitter_buffer_t *jb, uint8_t *data, uint16_t *seq);
void jitter_buffer_destroy(jitter_buffer_t *jb);

/* Platform must provide this: returns monotonic time in milliseconds */
uint32_t qmini_get_tick_count(void);

#endif
