#include "jitter_buffer.h"
#include <stdlib.h>
#include <string.h>

void jitter_buffer_init(jitter_buffer_t *jb) {
    memset(jb, 0, sizeof(*jb));
    jb->target_level = 4;
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

    int idx = jb->write_cursor & 0x7F;
    uint8_t *p = (uint8_t*)malloc(size);
    if (!p) return;
    memcpy(p, data, size);
    if (jb->packets[idx]) free(jb->packets[idx]);
    jb->packets[idx]      = p;
    jb->sizes[idx]        = size;
    jb->seq_numbers[idx]  = seq;
    jb->write_cursor++;
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
