#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_wps.h"
#include "nvs.h"
#include "integrations/wpa_supplicant/nintendo_pairing.h"
#include "network/pairing_ap.h"
#include "esp_private/wifi.h"

static void load_or_create_psk(char psk[65])
{
    nvs_handle_t store;
    ESP_ERROR_CHECK(nvs_open("toffee", NVS_READWRITE, &store));
    size_t size = 65;
    esp_err_t result = nvs_get_str(store, "psk", psk, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND)
    {
        uint8_t random_key[32];
        esp_fill_random(random_key, sizeof(random_key));
        for (size_t i = 0; i < sizeof(random_key); ++i)
            snprintf(psk + 2 * i, 3, "%02x", random_key[i]);
        memset(random_key, 0, sizeof(random_key));
        ESP_ERROR_CHECK(nvs_set_str(store, "psk", psk));
        ESP_ERROR_CHECK(nvs_commit(store));
    } else
    {
        ESP_ERROR_CHECK(result);
        assert(size == 65);
    }
    nvs_close(store);
}

static volatile int active_code = -1;

int pairing_ap_code(void) { return active_code; }

void pairing_code_save(uint8_t code)
{
    nvs_handle_t store;
    ESP_ERROR_CHECK(nvs_open("toffee", NVS_READWRITE, &store));
    ESP_ERROR_CHECK(nvs_set_u8(store, "pair_code", code));
    ESP_ERROR_CHECK(nvs_commit(store));
    nvs_close(store);
}

// The code the PC last chose; a bridge that never had one picks its own.
static uint8_t load_pairing_code(void)
{
    nvs_handle_t store;
    uint8_t code;
    if (nvs_open("toffee", NVS_READONLY, &store) == ESP_OK)
    {
        const esp_err_t result = nvs_get_u8(store, "pair_code", &code);
        nvs_close(store);
        if (result == ESP_OK)
            return code;
    }
    code = (uint8_t)esp_random();
    pairing_code_save(code);
    return code;
}

static void build_ssids(const uint8_t mac[6], uint8_t code, pairing_network_t* network)
{
    char mac_text[13];
    snprintf(mac_text, sizeof(mac_text), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
    snprintf(network->operational_ssid, sizeof(network->operational_ssid), "WiiU%s", mac_text);

    char pin_mac[13];
    snprintf(pin_mac, sizeof(pin_mac), "%.10s%02x", mac_text, code);
    snprintf(network->pairing_ssid, sizeof(network->pairing_ssid), "WiiU%.11s%s_STA1", pin_mac,
             pin_mac);
}

static void set_pairing_vendor_ies(void)
{
    static const uint8_t broadcom_ht_ie[] = {0xdd, 0x08, 0x00, 0x90, 0x4c,
                                             0x07, 0x00, 0x51, 0x32, 0x06};
    static const uint8_t broadcom_compat_ie[] = {0xdd, 0x09, 0x00, 0x10, 0x18, 0x02,
                                                 0x01, 0x00, 0x04, 0x00, 0x00};

    constexpr wifi_vendor_ie_type_t types[] = {
        WIFI_VND_IE_TYPE_BEACON,
        WIFI_VND_IE_TYPE_PROBE_RESP,
    };
    for (const auto type : types)
    {
        ESP_ERROR_CHECK(esp_wifi_set_vendor_ie(true, type, WIFI_VND_IE_ID_0, broadcom_ht_ie));
        ESP_ERROR_CHECK(esp_wifi_set_vendor_ie(true, type, WIFI_VND_IE_ID_1, broadcom_compat_ie));
    }
}

static void start_wps_registrar(const uint8_t mac[6], uint8_t code, const pairing_network_t* network)
{
    char psk[65];
    load_or_create_psk(psk);

    esp_wps_config_t wps = WPS_CONFIG_INIT_DEFAULT(WPS_TYPE_PIN);
    snprintf(wps.factory_info.manufacturer, sizeof(wps.factory_info.manufacturer), "Broadcom");
    snprintf(wps.factory_info.model_name, sizeof(wps.factory_info.model_name), "SoftAP");
    snprintf(wps.factory_info.model_number, sizeof(wps.factory_info.model_number), "0");
    snprintf(wps.factory_info.device_name, sizeof(wps.factory_info.device_name), "%s",
             network->operational_ssid);
    // The WPS PIN is the four symbols as digits, then 5678
    for (int i = 0; i < 4; ++i)
        wps.pin[i] = static_cast<char>('0' + ((code >> (6 - 2 * i)) & 3));
    memcpy(wps.pin + 4, "5678", 5);
    ESP_ERROR_CHECK(esp_wifi_ap_wps_enable(&wps));
    ESP_ERROR_CHECK(esp_wifi_ap_wps_start((const unsigned char*)wps.pin));
    ESP_ERROR_CHECK(pairing_start(mac, network->operational_ssid, psk, wps.pin));
    memset(psk, 0, sizeof(psk));
}

static void configure_ap(const uint8_t ap_mac[6], int channel, uint8_t code, pairing_network_t* network)
{
    build_ssids(ap_mac, code, network);
    wifi_config_t secured{};
    memcpy(secured.ap.ssid, "Toffee-C5-Test", sizeof("Toffee-C5-Test") - 1);
    secured.ap.ssid_len = sizeof("Toffee-C5-Test") - 1;
    secured.ap.channel = static_cast<uint8_t>(channel);
    secured.ap.authmode = WIFI_AUTH_WPA2_PSK;
    memcpy(secured.ap.password, "00000000000000000000000000000000",
           sizeof("00000000000000000000000000000000"));
    secured.ap.max_connection = 4;
    secured.ap.beacon_interval = 100;
    secured.ap.pmf_cfg.required = false;
    memcpy(secured.ap.ssid, network->pairing_ssid, strlen(network->pairing_ssid));
    secured.ap.ssid_len = strlen(network->pairing_ssid);
    secured.ap.dtim_period = 3;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &secured));
    ESP_ERROR_CHECK(esp_wifi_start());

    network->open_ap = secured;
    network->open_ap.ap.authmode = WIFI_AUTH_OPEN;
    memset(network->open_ap.ap.password, 0, sizeof(network->open_ap.ap.password));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &network->open_ap));
    ESP_ERROR_CHECK(esp_wifi_set_band_mode(WIFI_BAND_MODE_5G_ONLY));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11A | WIFI_PROTOCOL_11N));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20));
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(84));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
}

void pairing_ap_start(const uint8_t ap_mac[6], int channel, wifi_promiscuous_cb_t receive_callback,
                      pairing_network_t* network)
{
    const uint8_t code = load_pairing_code();
    configure_ap(ap_mac, channel, code, network);
    set_pairing_vendor_ies();
    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA,
    };
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filter));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(receive_callback));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    start_wps_registrar(ap_mac, code, network);
    active_code = code;
}

bool paired_profile_load(uint8_t* channel)
{
    nvs_handle_t store;
    if (nvs_open("toffee", NVS_READONLY, &store) != ESP_OK)
        return false;
    uint8_t paired = 0, saved_channel = 0;
    char psk[65];
    size_t size = sizeof(psk);
    const bool valid = nvs_get_u8(store, "paired", &paired) == ESP_OK && paired == 1 &&
                       nvs_get_u8(store, "chan", &saved_channel) == ESP_OK &&
                       (saved_channel == 36 || saved_channel == 40 || saved_channel == 44 ||
                        saved_channel == 48) &&
                       nvs_get_str(store, "psk", psk, &size) == ESP_OK && size == sizeof(psk);
    memset(psk, 0, sizeof(psk));
    nvs_close(store);
    if (valid)
        *channel = saved_channel;
    return valid;
}

void paired_profile_save(uint8_t channel)
{
    nvs_handle_t store;
    ESP_ERROR_CHECK(nvs_open("toffee", NVS_READWRITE, &store));
    ESP_ERROR_CHECK(nvs_set_u8(store, "chan", channel));
    ESP_ERROR_CHECK(nvs_set_u8(store, "paired", 1));
    ESP_ERROR_CHECK(nvs_commit(store));
    nvs_close(store);
}

void paired_profile_forget(void)
{
    nvs_handle_t store;
    if (nvs_open("toffee", NVS_READWRITE, &store) != ESP_OK)
        return;
    nvs_erase_key(store, "paired");
    nvs_erase_key(store, "chan");
    nvs_erase_key(store, "psk");
    nvs_commit(store);
    nvs_close(store);
}

void paired_ap_start(const uint8_t ap_mac[6], int channel, pairing_network_t* network)
{
    configure_ap(ap_mac, channel, 0, network);
    operational_ap_start(network, false);
}

uint8_t operational_ap_start(const pairing_network_t* network, bool wps_active)
{
    uint8_t channel = 0;
    wifi_second_chan_t second;
    ESP_ERROR_CHECK(esp_wifi_get_channel(&channel, &second));

    char psk[65];
    nvs_handle_t store;
    ESP_ERROR_CHECK(nvs_open("toffee", NVS_READONLY, &store));
    size_t size = sizeof(psk);
    ESP_ERROR_CHECK(nvs_get_str(store, "psk", psk, &size));
    nvs_close(store);
    assert(size == sizeof(psk));

    wifi_config_t operational = network->open_ap;
    memset(operational.ap.ssid, 0, sizeof(operational.ap.ssid));
    memcpy(operational.ap.ssid, network->operational_ssid, strlen(network->operational_ssid));
    operational.ap.ssid_len = strlen(network->operational_ssid);
    operational.ap.ssid_hidden = 1;
    operational.ap.channel = channel;
    operational.ap.authmode = WIFI_AUTH_WPA2_PSK;
    operational.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
    memcpy(operational.ap.password, psk, 64);

    if (wps_active)
    {
        active_code = -1;
        ESP_ERROR_CHECK(esp_wifi_ap_wps_disable());
        ESP_ERROR_CHECK(
            esp_wifi_set_vendor_ie(false, WIFI_VND_IE_TYPE_BEACON, WIFI_VND_IE_ID_0, NULL));
        ESP_ERROR_CHECK(
            esp_wifi_set_vendor_ie(false, WIFI_VND_IE_TYPE_BEACON, WIFI_VND_IE_ID_1, NULL));
        ESP_ERROR_CHECK(
            esp_wifi_set_vendor_ie(false, WIFI_VND_IE_TYPE_PROBE_RESP, WIFI_VND_IE_ID_0, NULL));
        ESP_ERROR_CHECK(
            esp_wifi_set_vendor_ie(false, WIFI_VND_IE_TYPE_PROBE_RESP, WIFI_VND_IE_ID_1, NULL));
    }
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &operational));
    ESP_ERROR_CHECK(esp_wifi_internal_set_fix_rate(WIFI_IF_AP, false, WIFI_PHY_RATE_6M));
    ESP_LOGI("pairing_ap", "Operational TX automatic rate selection enabled");
    memset(psk, 0, sizeof(psk));
    memset(operational.ap.password, 0, sizeof(operational.ap.password));

    return channel;
}
