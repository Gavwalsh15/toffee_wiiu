// Compiled inside the wpa_supplicant component to use its private types/options.
#include "utils/includes.h"
#include "utils/common.h"
#include "utils/wpabuf.h"
#include "wps/wps.h"
#include "wps/wps_i.h"
#include "esp_log.h"
#include "esp_private/wifi.h"
#include "esp_wifi_driver.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nintendo_pairing.h"
#include <stdatomic.h>

#define MAX_EAPOL 1600
#define MAX_WSC 4096
static const char *PAIR_TAG = "pairing";
static const uint8_t wsc_type[] = {254, 0, 0x37, 0x2a, 0, 0, 0, 1};
static uint8_t pin[8];
static struct wps_context context;
static struct wps_credential credential;
static struct wps_data *session;
static uint8_t own_mac[6], peer[6];
static char device_name[33];
static QueueHandle_t queue;
static uint8_t identifier, last_response_id;
static bool have_response, active;
static atomic_bool complete;
static uint8_t tx_cache[MAX_EAPOL + 14];
static size_t tx_length;
static TickType_t last_tx, session_started;
static uint8_t assembly[MAX_WSC];
static size_t assembly_length, assembly_expected;

bool pairing_complete(void)
{
    return atomic_load(&complete);
}

typedef struct {
    uint8_t source[6];
    uint16_t length;
    uint8_t bytes[MAX_EAPOL];
} eapol_packet_t;

static uint16_t read_be16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}
static void put16(uint8_t *p, size_t n)
{
    p[0] = n >> 8;
    p[1] = n;
}
static int rf_band(void *arg)
{
    return WPS_RF_50GHZ;
}

static void clear_session(void)
{
    if (session) {
        session->use_cred = NULL; // Borrowed static credential
        wps_deinit(session);
        session = NULL;
    }
    active = false;
    tx_length = 0;
    have_response = false;
    assembly_length = assembly_expected = 0;
}

static esp_err_t transmit_cached(void)
{
    if (!tx_length)
        return ESP_ERR_INVALID_STATE;
    esp_err_t result = esp_wifi_internal_tx(WIFI_IF_AP, tx_cache, tx_length);
    last_tx = xTaskGetTickCount();
    if (result != ESP_OK)
        ESP_LOGW(PAIR_TAG, "EAP TX failed: %s", esp_err_to_name(result));
    return result;
}

static void send_eap(uint8_t code, const uint8_t *payload, size_t length)
{
    if (length + 22 > sizeof(tx_cache)) {
        ESP_LOGE(PAIR_TAG, "Outgoing EAP too large (%u)", (unsigned)length);
        clear_session();
        return;
    }
    memcpy(tx_cache, peer, 6);
    memcpy(tx_cache + 6, own_mac, 6);
    tx_cache[12] = 0x88;
    tx_cache[13] = 0x8e;
    tx_cache[14] = 1;
    tx_cache[15] = 0;
    put16(tx_cache + 16, length + 4);
    tx_cache[18] = code;
    if (code == 1)
        ++identifier;
    tx_cache[19] = identifier;
    put16(tx_cache + 20, length + 4);
    if (length)
        memcpy(tx_cache + 22, payload, length);
    tx_length = length + 22;
    transmit_cached();
    ESP_LOGD(PAIR_TAG, "EAP TX code=%u id=%u bytes=%u", code, identifier, (unsigned)(length + 4));
}

static void send_wsc(uint8_t opcode, const uint8_t *message, size_t length)
{
    uint8_t payload[1450];
    if (length > sizeof(payload) - 10) {
        ESP_LOGE(PAIR_TAG, "WSC TX fragmentation not implemented for %u bytes", (unsigned)length);
        clear_session();
        return;
    }
    memcpy(payload, wsc_type, sizeof(wsc_type));
    payload[8] = opcode;
    payload[9] = 0;
    if (length)
        memcpy(payload + 10, message, length);
    send_eap(1, payload, length + 10);
}

static int message_type(const uint8_t *p, size_t n)
{
    while (n >= 4) {
        size_t len = read_be16(p + 2);
        if (len > n - 4)
            return -1;
        if (read_be16(p) == 0x1022 && len == 1)
            return p[4];
        p += len + 4;
        n -= len + 4;
    }
    return -1;
}

static int set_information_elements(void *arg, struct wpabuf *beacon, struct wpabuf *probe)
{
    const struct wpabuf *elements[] = {beacon, probe};
    int result = 0;
    for (int i = 0; i < 2; ++i) {
        if (!elements[i] || wpabuf_len(elements[i]) < 6) {
            result = -1;
            continue;
        }
        const uint8_t *p = wpabuf_head(elements[i]);
        if (p[0] != 221 || (size_t)p[1] + 2 != wpabuf_len(elements[i])) {
            ESP_LOGE(PAIR_TAG, "Unexpected WPS IE layout");
            result = -1;
            continue;
        }
        wifi_appie_ram_t appie = (i == 0) ? WIFI_APPIE_RAM_BEACON : WIFI_APPIE_RAM_PROBE_RSP;
        esp_wifi_unset_appie_internal(appie);
        esp_err_t err =
            esp_wifi_set_appie_internal(appie, (uint8_t *)p, wpabuf_len(elements[i]), 0);
        if (err != ESP_OK) {
            ESP_LOGE(PAIR_TAG, "WPS IE: %s", esp_err_to_name(err));
            result = -1;
        }
    }
    wpabuf_free(beacon);
    wpabuf_free(probe);
    return result;
}

static void registration_success(void *arg, const u8 *mac, const u8 *uuid, const u8 *password,
                                 size_t password_length)
{
    ESP_LOGI(PAIR_TAG, "WPS registration reported success for " MACSTR, MAC2STR(mac));
}

static void process_packet(const eapol_packet_t *packet)
{
    const uint8_t *p = packet->bytes;
    size_t n = packet->length;
    if (n < 4 || read_be16(p + 2) > n - 4)
        return;
    if (p[1] == 1) { // EAPOL-Start
        if (active) {
            if (!memcmp(peer, packet->source, 6))
                transmit_cached();
            return;
        }
        clear_session();
        memcpy(peer, packet->source, 6);
        active = true;
        session_started = xTaskGetTickCount();
        const uint8_t identity = 1;
        ESP_LOGI(PAIR_TAG, "EAPOL-START from=" MACSTR, MAC2STR(peer));
        send_eap(1, &identity, 1);
        return;
    }
    if (!active || memcmp(peer, packet->source, 6) || p[1] != 0)
        return;
    n = read_be16(p + 2);
    p += 4;
    if (n < 5 || p[0] != 2 || read_be16(p + 2) != n)
        return;
    if (have_response && p[1] == last_response_id) {
        transmit_cached();
        return;
    }
    if (p[1] != identifier)
        return;
    last_response_id = p[1];
    have_response = true;
    ESP_LOGD(PAIR_TAG, "EAP RX response id=%u type=%u bytes=%u", p[1], p[4], (unsigned)n);
    if (p[4] == 1 && !session) {
        static const char identity[] = "WFA-SimpleConfig-Enrollee-1-0";
        if (n - 5 != sizeof(identity) - 1 || memcmp(p + 5, identity, sizeof(identity) - 1)) {
            ESP_LOGW(PAIR_TAG, "Peer did not supply a WPS enrollee identity");
            clear_session();
            return;
        }
        struct wps_config config = {
            .wps = &context, .registrar = 1, .pin_len = 8, .dev_pw_id = DEV_PW_DEFAULT};
        memcpy(config.pin, pin, 8);
        session = wps_init(&config);
        if (!session) {
            clear_session();
            return;
        }
        memcpy(credential.mac_addr, peer, 6);
        session->use_cred = &credential;
        ESP_LOGI(PAIR_TAG, "WPS enrollee identity accepted; sending WSC_START");
        send_wsc(WSC_Start, NULL, 0);
        return;
    }
    if (!session) {
        ESP_LOGW(PAIR_TAG, "Expanded EAP arrived without a WPS session");
        return;
    }
    if (n < 14) {
        ESP_LOGW(PAIR_TAG, "Expanded EAP header too short: %u", (unsigned)n);
        return;
    }
    if (memcmp(p + 4, wsc_type, sizeof(wsc_type))) {
        ESP_LOGW(PAIR_TAG, "Unexpected EAP vendor header: %02x%02x%02x%02x%02x%02x%02x%02x", p[4],
                 p[5], p[6], p[7], p[8], p[9], p[10], p[11]);
        return;
    }
    uint8_t opcode = p[12], flags = p[13];
    if (flags & ~3) {
        // The physical GamePad sets 0x6c 
        ESP_LOGD(PAIR_TAG, "WSC peer flags=0x%02x; fragmentation bits=0x%02x", flags, flags & 3);
    }
    flags &= 3;
    p += 14;
    n -= 14;
    if (flags & 2) {
        if (n < 2 || assembly_expected) {
            clear_session();
            return;
        }
        assembly_expected = read_be16(p);
        p += 2;
        n -= 2;
        assembly_length = 0;
        if (!assembly_expected || assembly_expected > MAX_WSC) {
            clear_session();
            return;
        }
    }
    if (assembly_expected || (flags & 1)) {
        if (!assembly_expected || n > assembly_expected - assembly_length) {
            clear_session();
            return;
        }
        memcpy(assembly + assembly_length, p, n);
        assembly_length += n;
        if (flags & 1) {
            send_wsc(WSC_FRAG_ACK, NULL, 0);
            return;
        }
        if (assembly_length != assembly_expected) {
            clear_session();
            return;
        }
        p = assembly;
        n = assembly_length;
        assembly_length = assembly_expected = 0;
    }
    ESP_LOGD(PAIR_TAG, "WSC RX opcode=%u message_type=0x%02x bytes=%u", opcode, message_type(p, n),
             (unsigned)n);
    struct wpabuf *message = wpabuf_alloc_copy(p, n);
    if (!message) {
        clear_session();
        return;
    }
    enum wps_process_res result = wps_process_msg(session, opcode, message);
    wpabuf_free(message);
    if (result == WPS_DONE) {
        ESP_LOGI(PAIR_TAG, "WPS DONE: credentials delivered switching to operational AP");
        send_eap(4, NULL, 0); // WPS terminates EAP with Failure after WSC_Done
        atomic_store(&complete, true);
        clear_session();
        return;
    }
    if (result != WPS_CONTINUE) {
        ESP_LOGW(PAIR_TAG, "WPS processing stopped: result=%d state=%d", result, session->state);
        clear_session();
        return;
    }
    enum wsc_op_code reply_opcode;
    struct wpabuf *reply = wps_get_msg(session, &reply_opcode);
    if (!reply) {
        ESP_LOGW(PAIR_TAG, "No WPS reply available");
        clear_session();
        return;
    }
    ESP_LOGD(PAIR_TAG, "WSC TX opcode=%u message_type=0x%02x bytes=%u", reply_opcode,
             message_type(wpabuf_head(reply), wpabuf_len(reply)), (unsigned)wpabuf_len(reply));
    send_wsc(reply_opcode, wpabuf_head(reply), wpabuf_len(reply));
    wpabuf_free(reply);
}

static void pairing_task(void *arg)
{
    eapol_packet_t *packet = malloc(sizeof(*packet));
    assert(packet);
    TickType_t refreshed = xTaskGetTickCount();
    while (true) {
        if (xQueueReceive(queue, packet, pdMS_TO_TICKS(100)))
            process_packet(packet);
        TickType_t now = xTaskGetTickCount();
        if (active && now - session_started > pdMS_TO_TICKS(45000)) {
            ESP_LOGW(PAIR_TAG, "Pairing session timed out");
            clear_session();
        } else if (active && now - last_tx > pdMS_TO_TICKS(2000)) {
            transmit_cached();
        }
        if (!active && !pairing_complete() && now - refreshed > pdMS_TO_TICKS(90000)) {
            wps_registrar_flush(context.registrar);
            wps_registrar_add_pin(context.registrar, NULL, NULL, pin, 8, 0);
            refreshed = now;
        }
    }
}

void pairing_rx(const uint8_t *frame, size_t length)
{
    if (!queue || length < 36)
        return;
    uint16_t fc = frame[0] | ((uint16_t)frame[1] << 8);
    if ((fc & 0x000c) != 0x0008 || (fc & 0x0300) != 0x0100 || (fc & 0x4000))
        return;
    if ((fc & 0x0400) || (frame[22] & 0x0f))
        return; /* No 802.11 fragment reassembly. */
    if (memcmp(frame + 4, own_mac, 6) || (frame[0] & 0x40))
        return;
    size_t offset = 24;
    if (frame[0] & 0x80) {
        if (length < 26 || (frame[24] & 0x80))
            return; /* No A-MSDU. */
        offset += 2;
        if (fc & 0x8000)
            offset += 4;
    }
    const uint8_t llc[] = {0xaa, 0xaa, 3, 0, 0, 0, 0x88, 0x8e};
    if (length < offset + 12 || memcmp(frame + offset, llc, 8))
        return;
    const uint8_t *p = frame + offset + 8;
    size_t n = read_be16(p + 2) + 4;
    if (n > MAX_EAPOL || n > length - offset - 8)
        return;
    eapol_packet_t packet;
    memcpy(packet.source, frame + 10, 6);
    packet.length = n;
    memcpy(packet.bytes, p, n);
    xQueueSend(queue, &packet, 0);
}

esp_err_t pairing_start(const uint8_t ap_mac[6], const char *ssid, const char *psk, const char *pin_digits)
{
    memcpy(pin, pin_digits, sizeof(pin));
    memcpy(own_mac, ap_mac, 6);
    memcpy(context.dev.mac_addr, ap_mac, 6);
    static const uint8_t wiiu_uuid[16] = {
        0x22, 0x21, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    };
    memcpy(context.uuid, wiiu_uuid, sizeof(wiiu_uuid));
    snprintf(device_name, sizeof(device_name), "%s", ssid);
    context.ap = 1;
    context.rf_band_cb = rf_band;
    context.wps_state = WPS_STATE_CONFIGURED;
    context.config_methods = WPS_CONFIG_LABEL | WPS_CONFIG_PUSHBUTTON;
    context.auth_types = context.ap_auth_type = WPS_AUTH_WPA2PSK;
    context.encr_types = context.encr_types_rsn = context.ap_encr_type = WPS_ENCR_AES;
    context.dev.manufacturer = "Broadcom";
    context.dev.model_name = "SoftAP";
    context.dev.model_number = "0";
    context.dev.serial_number = "0";
    context.dev.device_name = device_name;
    context.dev.rf_bands = WPS_RF_50GHZ;
    context.dev.config_methods = context.config_methods;
    context.dev.os_version = 0x80000000;
    const uint8_t device_type[] = {0, 6, 0xa4, 0xc0, 0xe1, 0xf4, 0, 1};
    memcpy(context.dev.pri_dev_type, device_type, 8);
    context.ssid_len = strlen(ssid);
    if (context.ssid_len > sizeof(context.ssid) || strlen(psk) != 64)
        return ESP_ERR_INVALID_ARG;
    memcpy(context.ssid, ssid, context.ssid_len);
    credential.ssid_len = context.ssid_len;
    memcpy(credential.ssid, ssid, credential.ssid_len);
    credential.auth_type = WPS_AUTH_WPA2PSK;
    credential.encr_type = WPS_ENCR_AES;
    memcpy(credential.key, psk, 64);
    credential.key_len = 64;
    memcpy(credential.mac_addr, ap_mac, 6);
    struct wps_registrar_config cfg = {.set_ie_cb = set_information_elements,
                                       .reg_success_cb = registration_success,
                                       .disable_auto_conf = 1,
                                       .dualband = 1};
    context.registrar = wps_registrar_init(&context, &cfg);
    if (!context.registrar)
        return ESP_FAIL;
    if (wps_registrar_add_pin(context.registrar, NULL, NULL, pin, 8, 0) < 0)
        return ESP_FAIL;
    queue = xQueueCreate(8, sizeof(eapol_packet_t));
    if (!queue)
        return ESP_ERR_NO_MEM;
    if (xTaskCreate(pairing_task, "wps", 8192, NULL, 4, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}
