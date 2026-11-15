#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char pairing_ssid[33];
    char operational_ssid[33];
    wifi_config_t open_ap;
} pairing_network_t;

void pairing_ap_start(const uint8_t ap_mac[6], int channel, wifi_promiscuous_cb_t receive_callback,
                      pairing_network_t* network);

/* wps_active: pairing_ap_start ran first, so its WPS registrar and vendor elements must be torn
 * down. */
uint8_t operational_ap_start(const pairing_network_t* network, bool wps_active);

/* The pairing code (common/wire_limits.h) while the pairing network is up, or -1. */
int pairing_ap_code(void);
void pairing_code_save(uint8_t code);

bool paired_profile_load(uint8_t* channel);
void paired_profile_save(uint8_t channel);
void paired_profile_forget(void);

void paired_ap_start(const uint8_t ap_mac[6], int channel, pairing_network_t* network);

#ifdef __cplusplus
}
#endif
