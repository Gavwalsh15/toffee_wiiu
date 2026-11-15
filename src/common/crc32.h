#pragma once
// The standard CRC-32 (IEEE 802.3)
#include <stddef.h>
#include <stdint.h>

static inline uint32_t toffee_crc32(const void* data, size_t length)
{
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
