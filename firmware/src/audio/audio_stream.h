#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void audio_stream_start(void);
void audio_stream_set_active(bool active);
void audio_stream_push(const uint8_t* data, size_t length);

void audio_stream_vibrate(uint32_t duration_ms);

#ifdef __cplusplus
}
#endif
