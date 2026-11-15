#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t video_stream_start(void);
void video_stream_set_ready(bool ready);

void video_stream_poll_feedback(void);
void video_stream_request_idr(void);

/* Shared with the audio sender: pad-bound packets use the bound 50021 socket. */
int video_stream_audio_socket(void);
bool video_stream_gamepad_ready(void);

#ifdef __cplusplus
}
#endif
