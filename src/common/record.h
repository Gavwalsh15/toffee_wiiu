#pragma once
#include "common/crc32.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RECORD_HEADER_SIZE 20u

static inline void record_put_u32(uint8_t* out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static inline uint32_t record_get_u32(const uint8_t* in)
{
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

static inline size_t record_encode(uint8_t* out, const char magic[4], uint8_t type, uint32_t sequence,
                                   const void* payload, size_t length)
{
    memcpy(out, magic, 4);
    out[4] = type;
    out[5] = out[6] = out[7] = 0;
    record_put_u32(out + 8, sequence);
    record_put_u32(out + 12, (uint32_t)length);
    record_put_u32(out + 16, toffee_crc32(payload, length));
    if (length)
        memcpy(out + RECORD_HEADER_SIZE, payload, length);
    return RECORD_HEADER_SIZE + length;
}
