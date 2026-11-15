#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// The eight WPS PIN digits
esp_err_t pairing_start(const uint8_t ap_mac[6], const char* operational_ssid, const char* psk_hex,
                        const char* pin);
void pairing_rx(const uint8_t* frame, size_t length);
bool pairing_complete(void);

#ifdef __cplusplus
}
#endif
