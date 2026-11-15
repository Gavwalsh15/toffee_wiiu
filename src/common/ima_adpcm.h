#pragma once
#include "wire_limits.h"
#include <stddef.h>
#include <stdint.h>

#define ADPCM_FRAMES (AUDIO_CHUNK_BYTES / 4u)
#define ADPCM_BLOCK_BYTES (8u + ADPCM_FRAMES)

typedef struct {
    int16_t predictor;
    uint8_t index;
} adpcm_channel_t;

static const int16_t adpcm_steps[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t adpcm_index_change[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

static inline int16_t adpcm_decode_nibble(adpcm_channel_t* c, uint8_t nibble)
{
    const int step = adpcm_steps[c->index];
    int diff = step >> 3;
    if (nibble & 4)
        diff += step;
    if (nibble & 2)
        diff += step >> 1;
    if (nibble & 1)
        diff += step >> 2;
    int predictor = c->predictor + ((nibble & 8) ? -diff : diff);
    predictor = predictor < -32768 ? -32768 : predictor > 32767 ? 32767 : predictor;
    c->predictor = (int16_t)predictor;
    const int index = c->index + adpcm_index_change[nibble & 7];
    c->index = (uint8_t)(index < 0 ? 0 : index > 88 ? 88 : index);
    return c->predictor;
}

static inline uint8_t adpcm_encode_sample(adpcm_channel_t* c, int16_t sample)
{
    const int step = adpcm_steps[c->index];
    int diff = sample - c->predictor;
    uint8_t nibble = 0;
    if (diff < 0)
    {
        nibble = 8;
        diff = -diff;
    }
    if (diff >= step)
    {
        nibble |= 4;
        diff -= step;
    }
    if (diff >= step >> 1)
    {
        nibble |= 2;
        diff -= step >> 1;
    }
    if (diff >= step >> 2)
        nibble |= 1;
    adpcm_decode_nibble(c, nibble);
    return nibble;
}

static inline void adpcm_write_state(uint8_t* out, const adpcm_channel_t* c)
{
    out[0] = (uint8_t)c->predictor;
    out[1] = (uint8_t)((uint16_t)c->predictor >> 8);
    out[2] = c->index;
    out[3] = 0;
}

static inline void adpcm_encode_block(adpcm_channel_t state[2], const uint8_t* pcm, uint8_t* out)
{
    adpcm_write_state(out, &state[0]);
    adpcm_write_state(out + 4, &state[1]);
    for (size_t i = 0; i < ADPCM_FRAMES; ++i)
    {
        const int16_t left = (int16_t)(pcm[4 * i] | pcm[4 * i + 1] << 8);
        const int16_t right = (int16_t)(pcm[4 * i + 2] | pcm[4 * i + 3] << 8);
        out[8 + i] = (uint8_t)(adpcm_encode_sample(&state[0], left) |
                               adpcm_encode_sample(&state[1], right) << 4);
    }
}

static inline int adpcm_decode_block(const uint8_t* in, uint8_t* pcm)
{
    adpcm_channel_t state[2];
    for (int ch = 0; ch < 2; ++ch)
    {
        state[ch].predictor = (int16_t)(in[4 * ch] | in[4 * ch + 1] << 8);
        state[ch].index = in[4 * ch + 2];
        if (state[ch].index > 88)
            return 0;
    }
    for (size_t i = 0; i < ADPCM_FRAMES; ++i)
    {
        const int16_t left = adpcm_decode_nibble(&state[0], in[8 + i] & 15);
        const int16_t right = adpcm_decode_nibble(&state[1], in[8 + i] >> 4);
        pcm[4 * i] = (uint8_t)left;
        pcm[4 * i + 1] = (uint8_t)((uint16_t)left >> 8);
        pcm[4 * i + 2] = (uint8_t)right;
        pcm[4 * i + 3] = (uint8_t)((uint16_t)right >> 8);
    }
    return 1;
}
