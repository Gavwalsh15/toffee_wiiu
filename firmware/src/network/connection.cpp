#include "connection.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "integrations/wpa_supplicant/nintendo_pairing.h"
#include "network/pairing_ap.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = "toffee";
static QueueHandle_t rx_queue;

typedef struct {
    uint8_t subtype;
    char ssid[33];
} management_event_t;

static void receive_management(void* buffer, wifi_promiscuous_pkt_type_t type)
{
    if (pairing_complete())
        return;
    if (type == WIFI_PKT_DATA)
    {
        const auto* packet = static_cast<const wifi_promiscuous_pkt_t*>(buffer);
        if (packet->rx_ctrl.sig_len >= 4)
            pairing_rx(packet->payload, packet->rx_ctrl.sig_len - 4);
        return;
    }

    if (type != WIFI_PKT_MGMT)
        return;
    const auto* packet = static_cast<const wifi_promiscuous_pkt_t*>(buffer);
    /* sig_len includes the four-byte FCS. */
    if (packet->rx_ctrl.sig_len < 28)
        return;
    const uint8_t* frame = packet->payload;
    uint8_t subtype = (frame[0] >> 4) & 0x0f;
    if (subtype != 0 && subtype != 4)
        return;
    management_event_t event = {.subtype = subtype};
    if (subtype == 4)
    {
        size_t pos = 24;
        size_t frame_end = packet->rx_ctrl.sig_len - 4;
        while (pos + 2 <= frame_end)
        {
            size_t len = frame[pos + 1];
            if (pos + 2 + len > frame_end)
                break;
            if (frame[pos] == 0 && len <= 32)
            {
                memcpy(event.ssid, frame + pos + 2, len);
                event.ssid[len] = '\0';
                break;
            }
            pos += 2 + len;
        }
    }
    xQueueSend(rx_queue, &event, 0);
}

void forget_gamepad_and_restart(uint8_t pairing_code)
{
    ESP_LOGW(TAG, "The PC asked to pair a new GamePad: forgetting the old one and restarting");
    pairing_code_save(pairing_code);
    paired_profile_forget();
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
}

void connect_gamepad(const uint8_t mac[6])
{
    uint8_t saved_channel = 0;
    if (paired_profile_load(&saved_channel))
    {
        pairing_network_t saved;
        paired_ap_start(mac, saved_channel, &saved);
        ESP_LOGI(TAG, "PAIRED AP ssid=%s bssid=" MACSTR " channel=%u hidden=1 WPA2/CCMP; no pairing needed",
                 saved.operational_ssid, MAC2STR(mac), saved_channel);
        return;
    }

    const int channel = CONFIG_CHANNEL;
    if (channel != 36 && channel != 40 && channel != 44 && channel != 48)
    {
        ESP_LOGE(TAG, "Choose channel 36, 40, 44, or 48");
        abort();
    }

    rx_queue = xQueueCreate(16, sizeof(management_event_t));
    assert(rx_queue);
    pairing_network_t network;
    pairing_ap_start(mac, channel, receive_management, &network);

    static const uint8_t pairing_channels[] = {36, 40, 44, 48};
    size_t channel_index = 0;
    while (pairing_channels[channel_index] != channel)
        ++channel_index;
    TickType_t last_channel_switch = xTaskGetTickCount();
    bool pairing_activity_seen = false;
    ESP_LOGI(TAG, "Pairing AP ready on channel %d; pairing code %02x", channel, pairing_ap_code());

    while (true)
    {
        management_event_t event;
        if (xQueueReceive(rx_queue, &event, pdMS_TO_TICKS(200)))
        {
            if (event.subtype == 0 ||
                (event.subtype == 4 && strcmp(event.ssid, network.pairing_ssid) == 0))
            {
                if (!pairing_activity_seen)
                    ESP_LOGI(TAG, "GamePad pairing activity detected; holding channel %u",
                             pairing_channels[channel_index]);
                pairing_activity_seen = true;
            }
        }

        if (pairing_complete())
        {
            uint8_t operational_channel = operational_ap_start(&network, true);
            paired_profile_save(operational_channel);
            ESP_LOGI(TAG, "OPERATIONAL AP ssid=%s bssid=" MACSTR " channel=%u hidden=1 WPA2/CCMP",
                     network.operational_ssid, MAC2STR(mac), operational_channel);
            return;
        }
        if (!pairing_activity_seen &&
            xTaskGetTickCount() - last_channel_switch >= pdMS_TO_TICKS(6000))
        {
            channel_index =
                (channel_index + 1) % (sizeof(pairing_channels) / sizeof(pairing_channels[0]));
            ESP_ERROR_CHECK(
                esp_wifi_set_channel(pairing_channels[channel_index], WIFI_SECOND_CHAN_NONE));
            last_channel_switch = xTaskGetTickCount();
        }
    }
}
