#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "common/wire_limits.h"

/* The stationary asset retains its tested original clock packet. */
static inline void standby_format(uint8_t packet[32], uint32_t timestamp)
{
    memset(packet, 0, 32);
    packet[0] = 0x04;
    packet[4] = 0x11;
    packet[5] = 0x11;
    packet[6] = 0x81;
    packet[7] = 0x08;
    packet[8] = timestamp;
    packet[9] = timestamp >> 8;
    packet[10] = timestamp >> 16;
    packet[11] = timestamp >> 24;
    packet[12] = packet[16] = 0x80;
    packet[13] = packet[17] = 0x3e;
    packet[20] = packet[24] = 0x08;
    packet[21] = packet[25] = 0x52;
}

/* Confirmed 59.94-Hz clock: mc_video=0, mc_sync=16000, video_format=0. */
static inline void video_format(uint8_t packet[32], uint32_t timestamp)
{
    memset(packet, 0, 32);
    packet[0] = 4;
    packet[3] = 24;
    packet[6] = 0x10;
    packet[8] = timestamp;
    packet[9] = timestamp >> 8;
    packet[10] = timestamp >> 16;
    packet[11] = timestamp >> 24;
    packet[20] = packet[24] = 0x80;
    packet[21] = packet[25] = 0x3e;
}

static inline void vstrm_header(uint8_t packet[16], uint16_t sequence, uint32_t timestamp,
                                uint16_t size, unsigned chunk, bool idr, bool init,
                                bool first_fragment, bool last_fragment)
{
    memset(packet, 0, 16);
    packet[0] = 0xf0 | ((sequence >> 8) & 3);
    packet[1] = sequence;
    packet[2] = 0x08 | (init ? 0x80 : 0) | (chunk == 0 && first_fragment ? 0x40 : 0) |
                (last_fragment ? 0x20 : 0) | (chunk == 4 && last_fragment ? 0x10 : 0) |
                ((size >> 8) & 7);
    packet[3] = size;
    packet[4] = timestamp >> 24;
    packet[5] = timestamp >> 16;
    packet[6] = timestamp >> 8;
    packet[7] = timestamp;
    packet[8] = 0x83;
    packet[9] = 0x85;
    packet[10] = 6;
    if (idr)
    {
        packet[11] = 0x80;
        packet[12] = 0x82;
    } else
    {
        packet[11] = 0x82;
    }
}

static inline uint16_t vstrm_fragment(uint8_t* packet, uint16_t sequence, uint32_t timestamp,
                                      const uint8_t* payload, uint16_t chunk_size, uint16_t offset,
                                      unsigned chunk, bool idr, bool init)
{
    if (offset >= chunk_size)
        return 0;
    const uint16_t remaining = chunk_size - offset;
    const uint16_t size =
        remaining > MAX_VIDEO_PACKET_PAYLOAD ? MAX_VIDEO_PACKET_PAYLOAD : remaining;
    vstrm_header(packet, sequence, timestamp, size, chunk, idr, init, offset == 0,
                 size == remaining);
    memcpy(packet + 16, payload + offset, size);
    return size;
}
