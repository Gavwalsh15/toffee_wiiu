#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void connect_gamepad(const uint8_t ap_mac[6]);

void forget_gamepad_and_restart(uint8_t pairing_code);

#ifdef __cplusplus
}
#endif
