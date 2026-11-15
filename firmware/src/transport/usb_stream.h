#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common/wire_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { USB_RECORD_SLOTS = MAX_OUTSTANDING_RECORDS };

typedef struct {
    uint8_t type;
    uint32_t sequence;
    const uint8_t* payload;
    size_t length;
    int64_t receive_us, reserve_us, validate_us, queued_at, dispatched_at;
    void* storage;
} usb_record_t;

void usb_stream_start(void);
/* Reserve before STREAM_READY. END/readiness loss safely releases retention. */
bool usb_stream_prepare_session(void);
void usb_stream_finish_session(void);
bool usb_stream_take(usb_record_t* record, uint32_t timeout_ms);
void usb_stream_complete(usb_record_t* record);

#ifdef __cplusplus
}
#endif
