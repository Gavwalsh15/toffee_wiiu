#pragma once

#include "common/ima_adpcm.h"
#include "common/wire_limits.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Platform-independent framing. reserve may block for backpressure; finish
 * takes ownership of storage. A NULL reservation still drains the record. */
typedef struct {
    uint8_t header[20];
    size_t header_used;
    size_t magic_used;
    uint8_t type;
    uint32_t sequence, length, expected_crc;
    uint8_t* payload;
    size_t payload_used;
    void* context;
    uint8_t* (*reserve)(void*, uint8_t, uint32_t, uint32_t);
    void (*finish)(void*, uint32_t);
} record_parser_t;

static inline uint32_t record_u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void record_reset(record_parser_t* p)
{
    p->header_used = p->magic_used = p->payload_used = 0;
    p->payload = NULL;
}

static inline void record_feed(record_parser_t* p, const uint8_t* bytes, size_t count)
{
    static const uint8_t magic[4] = {'T', 'V', 'U', '1'};
    while (count)
    {
        if (p->header_used == sizeof(p->header))
        {
            size_t span = p->length - p->payload_used;
            if (span > count)
                span = count;
            if (p->payload)
                memcpy(p->payload + p->payload_used, bytes, span);
            p->payload_used += span;
            bytes += span;
            count -= span;
            if (p->payload_used == p->length)
            {
                p->finish(p->context, p->expected_crc);
                record_reset(p);
            }
            continue;
        }
        const uint8_t byte = *bytes++;
        --count;
        if (p->header_used < sizeof(magic))
        {
            if (byte == magic[p->magic_used])
            {
                p->header[p->magic_used++] = byte;
                if (p->magic_used == sizeof(magic))
                {
                    p->header_used = sizeof(magic);
                    p->magic_used = 0;
                }
            } else
            {
                p->magic_used = byte == magic[0] ? 1 : 0;
                if (p->magic_used)
                    p->header[0] = byte;
            }
            continue;
        }
        p->header[p->header_used++] = byte;
        if (p->header_used != sizeof(p->header))
            continue;
        p->type = p->header[4];
        p->sequence = record_u32(p->header + 8);
        p->length = record_u32(p->header + 12);
        p->expected_crc = record_u32(p->header + 16);
        if (p->type < STREAM_BEGIN || p->type > PAIR_RESET ||
            (p->type > STREAM_END && p->type < VIBRATE) || p->header[5] || p->header[6] ||
            p->header[7] || p->length > MAX_USB_PAYLOAD ||
            (p->type == STREAM_BEGIN && p->length != 32) ||
            (p->type == VIBRATE && p->length != 4) ||
            (p->type == PAIR_RESET && p->length != PAIR_RESET_BYTES) ||
            (p->type == AUDIO_DATA_ADPCM && p->length != ADPCM_BLOCK_BYTES) ||
            (p->type == VIDEO_FRAME && p->length < DRH_RECORD_OVERHEAD) ||
            (p->type == STREAM_END && p->length != 0))
        {
            /* The bytes after the first may hold the start of a real record, for example when the
             * one before it was cut short: look through them again. */
            uint8_t again[sizeof(p->header) - 1];
            memcpy(again, p->header + 1, sizeof(again));
            record_reset(p);
            record_feed(p, again, sizeof(again));
            continue;
        }
        p->payload = p->reserve(p->context, p->type, p->sequence, p->length);
        if (!p->length)
        {
            p->finish(p->context, p->expected_crc);
            record_reset(p);
        }
    }
}
