#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One leased large record. The caller serialises every operation. Retaining
 * at BEGIN removes per-picture allocation; END defers freeing a live lease. */
typedef struct {
    uint8_t* data;
    size_t capacity;
    bool retained, leased;
    void* context;
    uint8_t* (*allocate)(void*, size_t);
    void (*deallocate)(void*, uint8_t*);
} large_buffer_t;

static inline void large_buffer_free(large_buffer_t* buffer)
{
    if (buffer->data)
        buffer->deallocate(buffer->context, buffer->data);
    buffer->data = NULL;
    buffer->capacity = 0;
}

static inline bool large_buffer_retain(large_buffer_t* buffer, size_t capacity)
{
    if (buffer->leased && (!buffer->data || buffer->capacity < capacity))
        return false;
    if (!buffer->data || buffer->capacity < capacity)
    {
        buffer->retained = false;
        large_buffer_free(buffer);
        buffer->data = buffer->allocate(buffer->context, capacity);
        buffer->capacity = buffer->data ? capacity : 0;
    }
    buffer->retained = buffer->data != NULL;
    return buffer->retained;
}

static inline uint8_t* large_buffer_acquire(large_buffer_t* buffer, size_t size)
{
    assert(!buffer->leased);
    buffer->leased = true;
    if (!buffer->retained)
    {
        large_buffer_free(buffer);
        buffer->data = buffer->allocate(buffer->context, size);
        buffer->capacity = buffer->data ? size : 0;
    }
    return buffer->capacity >= size ? buffer->data : NULL;
}

static inline void large_buffer_release(large_buffer_t* buffer)
{
    assert(buffer->leased);
    buffer->leased = false;
    if (!buffer->retained)
        large_buffer_free(buffer);
}

static inline void large_buffer_end(large_buffer_t* buffer)
{
    buffer->retained = false;
    if (!buffer->leased)
        large_buffer_free(buffer);
}
