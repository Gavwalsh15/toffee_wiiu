#include "bridge_link.h"
#include "common/bridge_record.h"
#include "network/pairing_ap.h"
#include "video/video_stream.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

/* A record waits this long for room in the USB buffer before it is dropped. */
#define WAIT_MS 20

static const char* TAG = "bridge_link";
static uint32_t boot_id;
static volatile uint32_t dropped;

void bridge_send(uint8_t type, uint32_t sequence, const void* payload, size_t length)
{
    uint8_t record[RECORD_HEADER_SIZE + BRIDGE_MAX_PAYLOAD];
    const size_t size = bridge_encode(record, type, sequence, payload, length);
    /* The USB driver queues a whole write or none of it, so log text can land between records but
     * never inside one, and the PC finds each record by its magic and checksum. */
    if (!size || !usb_serial_jtag_is_connected() ||
        usb_serial_jtag_write_bytes(record, size, pdMS_TO_TICKS(WAIT_MS)) != (int)size)
        ++dropped;
}

void bridge_send_state(void)
{
    const int code = pairing_ap_code();
    const uint8_t payload[4] = {video_stream_gamepad_ready() ? (uint8_t)1 : (uint8_t)0,
                                code >= 0 ? (uint8_t)1 : (uint8_t)0, (uint8_t)(code >= 0 ? code : 0), 0};
    bridge_send(BRIDGE_STATE, 0, payload, sizeof(payload));
}

void bridge_send_error(const char* message)
{
    ESP_LOGE(TAG, "PC_STREAM_ERROR:%s", message);
    bridge_send(BRIDGE_ERROR, 0, message, strnlen(message, BRIDGE_MAX_PAYLOAD));
}

uint32_t bridge_dropped_records(void) { return dropped; }

static void announce_task(void* unused)
{
    (void)unused;
    uint8_t hello[8] = {BRIDGE_PROTOCOL_VERSION & 0xff, BRIDGE_PROTOCOL_VERSION >> 8, 0, 0};
    record_put_u32(hello + 4, boot_id);
    while (true)
    {
        bridge_send(BRIDGE_HELLO, 0, hello, sizeof(hello));
        bridge_send_state();
        vTaskDelay(pdMS_TO_TICKS(BRIDGE_STATE_PERIOD_MS));
    }
}

void bridge_link_start(void)
{
    boot_id = esp_random();
    if (xTaskCreate(announce_task, "bridge_state", 2560, NULL, 3, NULL) != pdPASS)
        abort();
}
