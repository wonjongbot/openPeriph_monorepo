/**
 * @file    ring_buffer.h
 */

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ---------- Configuration ---------- */
/* Must be power of 2 for fast masking.
 * 4096 bytes handles a typical image chunk or email payload. */
#define RING_BUF_SIZE   4096U
#define RING_BUF_MASK   (RING_BUF_SIZE - 1U)

/* ---------- Type ---------- */
typedef struct {
    uint8_t  buf[RING_BUF_SIZE];
    volatile uint32_t head;   /* write index (producer) */
    volatile uint32_t tail;   /* read  index (consumer) */
} RingBuffer_t;

/* ---------- API ---------- */

/** Initialise / reset the ring buffer */
static inline void RingBuf_Init(RingBuffer_t *rb)
{
    rb->head = 0;
    rb->tail = 0;
    memset(rb->buf, 0, RING_BUF_SIZE);
}

/** Number of bytes available to read */
static inline uint32_t RingBuf_Available(const RingBuffer_t *rb)
{
    return (rb->head - rb->tail) & RING_BUF_MASK;
}

/** Free space available for writing */
static inline uint32_t RingBuf_FreeSpace(const RingBuffer_t *rb)
{
    return (RING_BUF_SIZE - 1U) - RingBuf_Available(rb);
}

static inline bool RingBuf_IsEmpty(const RingBuffer_t *rb)
{
    return rb->head == rb->tail;
}

static inline bool RingBuf_IsFull(const RingBuffer_t *rb)
{
    return RingBuf_Available(rb) == (RING_BUF_SIZE - 1U);
}

/** Write one byte. Returns false if full. */
static inline bool RingBuf_WriteByte(RingBuffer_t *rb, uint8_t byte)
{
    if (RingBuf_IsFull(rb)) return false;
    rb->buf[rb->head & RING_BUF_MASK] = byte;
    rb->head = (rb->head + 1U) & RING_BUF_MASK;
    return true;
}

/** Read one byte. Returns false if empty. */
static inline bool RingBuf_ReadByte(RingBuffer_t *rb, uint8_t *byte)
{
    if (RingBuf_IsEmpty(rb)) return false;
    *byte = rb->buf[rb->tail & RING_BUF_MASK];
    rb->tail = (rb->tail + 1U) & RING_BUF_MASK;
    return true;
}

/**
 * Write a block of bytes. Returns number of bytes actually written.
 * Partial writes are possible if buffer fills up.
 */
static inline uint32_t RingBuf_Write(RingBuffer_t *rb,
                                     const uint8_t *data, uint32_t len)
{
    uint32_t free = RingBuf_FreeSpace(rb);
    if (len > free) len = free;

    for (uint32_t i = 0; i < len; i++) {
        rb->buf[rb->head & RING_BUF_MASK] = data[i];
        rb->head = (rb->head + 1U) & RING_BUF_MASK;
    }
    return len;
}

/**
 * Read a block of bytes. Returns number of bytes actually read.
 */
static inline uint32_t RingBuf_Read(RingBuffer_t *rb,
                                    uint8_t *data, uint32_t len)
{
    uint32_t avail = RingBuf_Available(rb);
    if (len > avail) len = avail;

    for (uint32_t i = 0; i < len; i++) {
        data[i] = rb->buf[rb->tail & RING_BUF_MASK];
        rb->tail = (rb->tail + 1U) & RING_BUF_MASK;
    }
    return len;
}

/**
 * Peek at data without consuming it. Returns bytes peeked.
 */
//static inline uint32_t RingBuf_Peek(const RingBuffer_t *rb,
//                                    uint8_t *data, uint32_t len)
//{
//    uint32_t avail = RingBuf_Available(rb);
//    if (len > avail) len = avail;
//
//    uint32_t t = rb->tail;
//    for (uint32_t i = 0; i < len; i++) {
//        data[i] = rb->buf[t & RING_BUF_MASK];
//        t = (t + 1U) & RING_BUF_MASK;
//    }
//    return len;
//}
//
#endif /* RING_BUFFER_H */
