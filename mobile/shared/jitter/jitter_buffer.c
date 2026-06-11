/**
 * Adaptive jitter buffer implementation.
 * Portable version - uses qmini_get_tick_count() instead of GetTickCount().
 */
#include "jitter_buffer.h"
#include <string.h>
#include <math.h>

void jitter_buffer_init(jitter_buffer_t *jb) {
    memset(jb, 0, sizeof(*jb));
    jb->target_level      = 2;
    jb->min_target        = 1;
    jb->max_target        = 12;
    jb->jitter_avg        = 20;
    jb->jitter_variance   = 0;
    jb->last_arrival_time = 0;
    jb->last_adaptation   = 0;
}

void jitter_buffer_destroy(jitter_buffer_t *jb) {
    memset(jb, 0, sizeof(*jb));
}

void jitter_buffer_push(jitter_buffer_t *jb, const uint8_t *src, int size, uint16_t seq) {
    if (jb->count >= JB_CAPACITY) return;
    if (size > JB_MAX_PACKET) size = JB_MAX_PACKET;
    if (size <= 0) return;

    uint32_t now = qmini_get_tick_count();

    if (jb->last_arrival_time != 0) {
        uint32_t interval = now - jb->last_arrival_time;
        int32_t diff = (int32_t)interval - 20;
        if (diff < 0) diff = -diff;
        jb->jitter_avg      = (jb->jitter_avg * 15 + (uint32_t)diff) >> 4;
        uint32_t diff_sq    = (uint32_t)(diff * diff);
        jb->jitter_variance = (jb->jitter_variance * 15 + diff_sq) >> 4;
    }
    jb->last_arrival_time = now;

    if (now - jb->last_adaptation >= 1000) {
        jb->last_adaptation = now;
        uint32_t stddev     = (uint32_t)sqrt((double)jb->jitter_variance);
        uint32_t target_ms  = jb->jitter_avg + (stddev * 3) / 2;
        int target_packets  = (int)(target_ms / 20);
        if (target_packets < jb->min_target) target_packets = jb->min_target;
        if (target_packets > jb->max_target) target_packets = jb->max_target;
        jb->target_level = target_packets;
    }

    int idx = (jb->read_cursor + jb->count) & (JB_CAPACITY - 1);
    memcpy(jb->data[idx], src, size);
    jb->sizes[idx]       = size;
    jb->seq_numbers[idx] = seq;
    jb->count++;
}

int jitter_buffer_pop(jitter_buffer_t *jb, uint8_t *dst, uint16_t *seq) {
    if (jb->count == 0) return 0;
    if (jb->count <= jb->target_level) return 0;

    int idx = jb->read_cursor & (JB_CAPACITY - 1);
    int size = jb->sizes[idx];
    memcpy(dst, jb->data[idx], size);
    if (seq) *seq = jb->seq_numbers[idx];

    jb->read_cursor++;
    jb->count--;
    return size;
}
