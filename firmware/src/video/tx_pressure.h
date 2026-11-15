#pragma once
#include <stdbool.h>
#include <stdint.h>

// Sender-owned counters: no heap walk, logging or allocation per datagram.
typedef struct {
    uint32_t sequence, picture_bytes, accepted_packets, accepted_bytes;
    uint32_t idr_frames, p_frames, idr_packets_max, p_packets_max, rejected;
    bool idr;
    int64_t started_us, budget_left_us;
} video_tx_pressure_t;
static inline void video_tx_begin(video_tx_pressure_t* p, uint32_t sequence, uint32_t bytes,
                                  bool idr, int64_t started)
{
    p->sequence = sequence;
    p->picture_bytes = bytes;
    p->idr = idr;
    p->started_us = started;
    p->accepted_packets = p->accepted_bytes = 0;
    p->budget_left_us = 0;
    if (bytes)
    {
        if (idr)
            ++p->idr_frames;
        else
            ++p->p_frames;
    }
}
static inline void video_tx_accepted(video_tx_pressure_t* p, uint32_t bytes)
{
    ++p->accepted_packets;
    p->accepted_bytes += bytes;
    if (!p->picture_bytes)
        return;
    uint32_t* maximum = p->idr ? &p->idr_packets_max : &p->p_packets_max;
    if (p->accepted_packets > *maximum)
        *maximum = p->accepted_packets;
}
