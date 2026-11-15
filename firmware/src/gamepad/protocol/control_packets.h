#pragma once

#include <stdint.h>
#include <string.h>

// UVC/UAC command fields are big-endian, its reply uses little-endian.
static inline void uvc_reset(uint8_t packet[48])
{
    memset(packet, 0, 48);
    packet[12] = 0x3e;
    packet[13] = 0x80; /* 16000 Hz */
}

static inline void uvc_apply_reply(uint8_t packet[48], const uint8_t reply[16])
{
    packet[6] = reply[1];
    packet[7] = reply[0];
    packet[8] = reply[3];
    packet[9] = reply[2];
    packet[4] = reply[12] != 0;
    packet[16] = reply[13];
    packet[17] = reply[14] != 0;
}
