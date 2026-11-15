#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Records to the PC (common/bridge_record.h). Call once the USB driver is installed; it starts
 * announcing HELLO and STATE every BRIDGE_STATE_PERIOD_MS. */
void bridge_link_start(void);

/* Sends one record. It is dropped, and counted, when the PC is not connected or not reading. */
void bridge_send(uint8_t type, uint32_t sequence, const void* payload, size_t length);

void bridge_send_state(void);

void bridge_send_error(const char* message);

uint32_t bridge_dropped_records(void);

#ifdef __cplusplus
}
#endif
