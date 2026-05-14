#include "jitter_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <math.h>

void jitter_buffer_init(jitter_buffer_t *jb) {
    memset(jb, 0, sizeof(*jb));
    jb->target_level    = 4;        /* Initial 4 packets */
    jb->min_target      = 2;        /* Minimum 2 packets */
    jb->max_target      = 12;       /* Maximum 12 packets */
    jb->jitter_avg      = 20;       /* Initial estimate 20ms */
    jb->jitter_variance = 0;
    jb->last_arrival_time = 0;
    jb->last_adaptation   = 0;
}

void jitter_buffer_destroy(jitter_buffer_t *jb) {
    for (int i = 0; i < JB_CAPACITY; i++) {
        if (jb->packets[i]) {
            free(jb->packets[i]);
            jb->packets[i] = NULL;
        }
    }
    memset(jb, 0, sizeof(*jb));
}

void jitter_buffer_push(jitter_buffer_t *jb, const uint8_t *data, int size, uint16_t seq) {
    if (jb->count >= JB_CAPACITY) return;

    /* --- Adaptive jitter estimation --- */
    uint32_t now = GetTickCount();
    if (jb->last_arrival_time != 0) {
        uint32_t interval = now - jb->last_arrival_time;
        /* Expected interval is 20ms per packet */
        int32_t diff = (int32_t)interval - 20;
        if (diff < 0) diff = -diff;
        /* Exponential moving average: alpha = 1/16 */
        jb->jitter_avg      = (jb->jitter_avg * 15 + (uint32_t)diff) >> 4;
        /* Variance EMA */
        uint32_t diff_sq    = (uint32_t)(diff * diff);
        jb->jitter_variance = (jb->jitter_variance * 15 + diff_sq) >> 4;
    }
    jb->last_arrival_time = now;

    /* Recalculate target_level every 1 second */
    if (now - jb->last_adaptation >= 1000) {
        jb->last_adaptation = now;
        uint32_t stddev     = (uint32_t)sqrt((double)jb->jitter_variance);
        uint32_t target_ms  = jb->jitter_avg + (stddev * 3) / 2; /* +1.5*stddev */
        int target_packets  = (int)(target_ms / 20);
        if (target_packets < jb->min_target) target_packets = jb->min_target;
        if (target_packets > jb->max_target) target_packets = jb->max_target;
        jb->target_level = target_packets;
    }
    /* --- End adaptive jitter --- */

    int idx = (jb->read_cursor + jb->count) & 0x7F;
    uint8_t *p = (uint8_t*)malloc(size);
    if (!p) return;
    memcpy(p, data, size);
    if (jb->packets[idx]) free(jb->packets[idx]);
    jb->packets[idx]      = p;
    jb->sizes[idx]        = size;
    jb->seq_numbers[idx]  = seq;
    jb->count++;
}

int jitter_buffer_pop(jitter_buffer_t *jb, uint8_t *data, uint16_t *seq) {
    if (jb->count == 0) return 0;
    if (jb->count <= jb->target_level) return 0;

    int idx = jb->read_cursor & 0x7F;
    if (!jb->packets[idx]) return 0;

    int size = jb->sizes[idx];
    memcpy(data, jb->packets[idx], size);
    if (seq) *seq = jb->seq_numbers[idx];

    free(jb->packets[idx]);
    jb->packets[idx] = NULL;
    jb->read_cursor++;
    jb->count--;
    return size;
}
