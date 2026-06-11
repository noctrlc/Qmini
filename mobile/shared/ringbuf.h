/**
 * Lock-free single-producer/single-consumer ring buffer.
 * Portable version - replaces Win32 MemoryBarrier with C11 atomics.
 */
#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>

typedef struct {
    uint8_t *buf;
    size_t   size;       /* power of 2 */
    size_t   mask;
    atomic_size_t head;  /* producer index */
    atomic_size_t tail;  /* consumer index */
} ringbuf_t;

static inline void ringbuf_init(ringbuf_t *rb, uint8_t *buf, size_t size) {
    rb->buf  = buf;
    rb->size = size;
    rb->mask = size - 1;
    atomic_store(&rb->head, 0);
    atomic_store(&rb->tail, 0);
}

static inline size_t ringbuf_avail(ringbuf_t *rb) {
    size_t h = atomic_load_explicit(&rb->head, memory_order_acquire);
    size_t t = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    return (h - t) & rb->mask;
}

static inline size_t ringbuf_space(ringbuf_t *rb) {
    size_t h = atomic_load_explicit(&rb->head, memory_order_relaxed);
    size_t t = atomic_load_explicit(&rb->tail, memory_order_acquire);
    return rb->size - 1 - ((h - t) & rb->mask);
}

static inline size_t ringbuf_push(ringbuf_t *rb, const uint8_t *data, size_t len) {
    size_t h = atomic_load_explicit(&rb->head, memory_order_relaxed);
    size_t t = atomic_load_explicit(&rb->tail, memory_order_acquire);
    size_t i;
    for (i = 0; i < len; i++) {
        if (((h - t) & rb->mask) >= rb->size - 1) break;
        rb->buf[h] = data[i];
        h = (h + 1) & rb->mask;
    }
    atomic_store_explicit(&rb->head, h, memory_order_release);
    return i;
}

static inline size_t ringbuf_pop(ringbuf_t *rb, uint8_t *data, size_t len) {
    size_t h = atomic_load_explicit(&rb->head, memory_order_acquire);
    size_t t = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    size_t i;
    for (i = 0; i < len; i++) {
        if (((h - t) & rb->mask) == 0) break;
        data[i] = rb->buf[t];
        t = (t + 1) & rb->mask;
    }
    atomic_store_explicit(&rb->tail, t, memory_order_release);
    return i;
}

#endif
