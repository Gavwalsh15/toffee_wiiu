#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Places pictures back to back in one buffer, so several large pictures can be in flight in the
 * memory one worst-case IDR needs. Pictures are freed in the order they were placed, except the
 * newest, which may be dropped while still arriving. The caller serialises every operation. */
#define PICTURE_RING_ITEMS 4

typedef struct {
    size_t capacity;
    size_t offset[PICTURE_RING_ITEMS], length[PICTURE_RING_ITEMS];
    unsigned first, count;
} picture_ring_t;

static inline void picture_ring_reset(picture_ring_t* ring, size_t capacity)
{
    ring->capacity = capacity;
    ring->first = ring->count = 0;
}

static inline unsigned picture_ring_index(const picture_ring_t* ring, unsigned n)
{
    return (ring->first + n) % PICTURE_RING_ITEMS;
}

static inline long picture_ring_place(picture_ring_t* ring, size_t length)
{
    if (!length || length > ring->capacity || ring->count == PICTURE_RING_ITEMS)
        return -1;
    size_t at = 0;
    if (ring->count)
    {
        const unsigned last = picture_ring_index(ring, ring->count - 1);
        const size_t oldest = ring->offset[ring->first];
        const size_t end = ring->offset[last] + ring->length[last];
        if (ring->offset[last] >= oldest)
        {
            // Free space is after the newest picture, then before the oldest.
            if (ring->capacity - end >= length)
                at = end;
            else if (oldest >= length)
                at = 0;
            else
                return -1;
        } else if (oldest - end >= length)
            at = end; // Already wrapped: the free space is between newest and oldest.
        else
            return -1;
    }
    const unsigned slot = picture_ring_index(ring, ring->count++);
    ring->offset[slot] = at;
    ring->length[slot] = length;
    return (long)at;
}

/* Frees the picture at offset; it must be the oldest or the newest. */
static inline bool picture_ring_free(picture_ring_t* ring, size_t offset)
{
    if (!ring->count)
        return false;
    const unsigned last = picture_ring_index(ring, ring->count - 1);
    if (ring->offset[ring->first] == offset)
    {
        ring->first = (ring->first + 1) % PICTURE_RING_ITEMS;
        --ring->count;
        return true;
    }
    if (ring->offset[last] == offset)
    {
        --ring->count;
        return true;
    }
    return false;
}
