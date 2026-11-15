#pragma once
#include "common/record.h"

#define BRIDGE_MAGIC "TVB1"
#define BRIDGE_MAX_PAYLOAD 256u
#define BRIDGE_PROTOCOL_VERSION 3u
#define BRIDGE_STATE_PERIOD_MS 1000u
#define BRIDGE_INPUT_REPORT_BYTES 128u

enum {
    BRIDGE_HELLO = 0x10,
    BRIDGE_STATE = 0x11,
    BRIDGE_FRAME_DONE = 0x12,
    BRIDGE_STREAM_READY = 0x13,
    BRIDGE_IDR_REQUEST = 0x14,
    BRIDGE_IDR_SENT = 0x15,
    BRIDGE_ERROR = 0x16,
    BRIDGE_INPUT = 0x17,
};

static inline size_t bridge_encode(uint8_t* out, uint8_t type, uint32_t sequence, const void* payload,
                                   size_t length)
{
    return length > BRIDGE_MAX_PAYLOAD ? 0 : record_encode(out, BRIDGE_MAGIC, type, sequence, payload, length);
}
