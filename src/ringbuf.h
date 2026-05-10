#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>
#include <stddef.h>
#include <windows.h>

typedef struct {
    uint8_t *buf;
    size_t   size;       /* power of 2 */
    size_t   mask;
    volatile LONG head;  /* producer index */
    volatile LONG tail;  /* consumer index */
} ringbuf_t;

static __inline void ringbuf_init(ringbuf_t *rb, uint8_t *buf, size_t size) {
    rb->buf  = buf;
    rb->size = size;
    rb->mask = size - 1;
    rb->head = 0;
    rb->tail = 0;
}

static __inline size_t ringbuf_avail(ringbuf_t *rb) {
    return ((size_t)(rb->head - rb->tail)) & rb->mask;
}

static __inline size_t ringbuf_space(ringbuf_t *rb) {
    return rb->size - 1 - (((size_t)(rb->head - rb->tail)) & rb->mask);
}

static __inline size_t ringbuf_push(ringbuf_t *rb, const uint8_t *data, size_t len) {
    size_t i;
    LONG head = rb->head;
    for (i = 0; i < len; i++) {
        size_t avail = ((size_t)(head - rb->tail)) & rb->mask;
        if (avail >= rb->size - 1) break;
        rb->buf[head] = data[i];
        head = (head + 1) & rb->mask;
    }
    MemoryBarrier();
    rb->head = head;
    return i;
}

static __inline size_t ringbuf_pop(ringbuf_t *rb, uint8_t *data, size_t len) {
    size_t i;
    LONG tail = rb->tail;
    for (i = 0; i < len; i++) {
        size_t avail = ((size_t)(rb->head - tail)) & rb->mask;
        if (avail == 0) break;
        data[i] = rb->buf[tail];
        tail = (tail + 1) & rb->mask;
    }
    MemoryBarrier();
    rb->tail = tail;
    return i;
}

#endif
