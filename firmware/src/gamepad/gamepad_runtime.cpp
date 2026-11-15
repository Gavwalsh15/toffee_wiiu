#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "gamepad/gamepad_runtime.h"
#include "network/udp.h"
#include "gamepad/protocol/control_packets.h"
#include "video/video_stream.h"
#include "gamepad/input_forwarder.h"
#include "transport/cpu_profile.h"
#include "diagnostics/memory_diagnostics.h"

#define CONSOLE_COMMAND_PORT 50023
#define GAMEPAD_COMMAND_PORT 50123
#define CONSOLE_INPUT_PORT 50022
#define CONSOLE_MESSAGE_PORT 50010
#define CONSOLE_SERVICE_PORT 50025
static const char* TAG = "runtime";

static uint16_t read_le16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

static void write_le16(uint8_t* p, uint16_t value)
{
    p[0] = value;
    p[1] = value >> 8;
}

static void send_command(int fd, const struct sockaddr_in* destination, uint16_t packet_type,
                         uint16_t query, uint16_t sequence, const uint8_t* payload,
                         size_t payload_size)
{
    uint8_t packet[8 + 48] = {0};
    if (payload_size > sizeof(packet) - 8)
        return;
    write_le16(packet, packet_type);
    write_le16(packet + 2, query);
    write_le16(packet + 4, payload_size);
    write_le16(packet + 6, sequence);
    if (payload_size)
        memcpy(packet + 8, payload, payload_size);
    ssize_t sent = sendto(fd, packet, 8 + payload_size, 0, (const struct sockaddr*)destination,
                          sizeof(*destination));
    if (sent < 0)
        ESP_LOGW(TAG, "command send failed: %d", errno);
}

static void runtime_task(void* unused)
{
    (void)unused;
    int command, input, message, service;
    for (;;)
    {
        command = udp_bind(CONSOLE_COMMAND_PORT);
        input = udp_bind(CONSOLE_INPUT_PORT);
        message = udp_bind(CONSOLE_MESSAGE_PORT);
        service = udp_bind(CONSOLE_SERVICE_PORT);
        if (command >= 0 && input >= 0 && message >= 0 && service >= 0)
            break;
        if (command >= 0)
            close(command);
        if (input >= 0)
            close(input);
        if (message >= 0)
            close(message);
        if (service >= 0)
            close(service);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    struct sockaddr_in gamepad = {.sin_family = AF_INET, .sin_port = htons(GAMEPAD_COMMAND_PORT)};
    inet_pton(AF_INET, "192.168.1.11", &gamepad.sin_addr);
    const uint8_t uic[] = {0x7e, 0x01, 0x00, 0x08, 0x00, 0x40, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00};
    uint8_t uvc[48];
    uvc_reset(uvc);
    uint16_t sequence = 0;
    bool uic_done = false, uvc_done = false;
    bool session_seen = false;
    TickType_t next_query = 0;
    TickType_t last_input = 0, last_control = 0, next_status = 0;
    uint32_t input_packets = 0, control_replies = 0;
    uint32_t resync_packets = 0, other_messages = 0, service_packets = 0;

    uint8_t buffer[1024];
    struct sockaddr_in source;
    socklen_t source_len;

    while (true)
    {
        cpu_profile_poll();
        memory_diagnostics_poll();
        video_stream_poll_feedback();
        TickType_t now = xTaskGetTickCount();
        wifi_sta_list_t stations;
        const bool station_list_valid = esp_wifi_ap_get_sta_list(&stations) == ESP_OK;
        const bool peer_present = !station_list_valid || stations.num != 0;
        if (!peer_present)
        {
            if (uvc_done)
            {
                video_stream_set_ready(false);
                uic_done = uvc_done = false;
                uvc_reset(uvc);
                ESP_LOGW(TAG, "GamePad traffic timed out; Wi-Fi peer absent, pausing video");
            }
        }
        if (uvc_done && now - last_input > pdMS_TO_TICKS(3000) &&
            now - last_control > pdMS_TO_TICKS(3000))
        {
            video_stream_set_ready(false);
            uic_done = uvc_done = false;
            uvc_reset(uvc);
            ESP_LOGW(TAG, "GamePad traffic timed out; pausing video until control returns");
        }
        if (session_seen && !uvc_done && now - last_input >= pdMS_TO_TICKS(30000) &&
            now - last_control >= pdMS_TO_TICKS(30000))
        {
            ESP_LOGW(TAG, "No GamePad input/control for 30 seconds; restarting into pairing");
            esp_restart();
        }
        if (uvc_done && now >= next_status)
        {
            ESP_LOGI(TAG,
                     "GamePad traffic: input=%lu replies=%lu input_age_ms=%lu control_age_ms=%lu "
                     "resync=%lu other_messages=%lu service=%lu",
                     (unsigned long)input_packets, (unsigned long)control_replies,
                     (unsigned long)((now - last_input) * portTICK_PERIOD_MS),
                     (unsigned long)((now - last_control) * portTICK_PERIOD_MS),
                     (unsigned long)resync_packets, (unsigned long)other_messages,
                     (unsigned long)service_packets);
            next_status = now + pdMS_TO_TICKS(10000);
        }
        if (peer_present && now >= next_query)
        {
            if (!uic_done)
                send_command(command, &gamepad, 0, 0, sequence++, uic, sizeof(uic));
            send_command(command, &gamepad, 0, 1, sequence++, uvc, sizeof(uvc));
            next_query = now + pdMS_TO_TICKS(1000);
        }

        fd_set readset;
        FD_ZERO(&readset);
        FD_SET(command, &readset);
        FD_SET(input, &readset);
        FD_SET(message, &readset);
        FD_SET(service, &readset);
        int maxfd = command > input ? command : input;
        if (message > maxfd)
            maxfd = message;
        if (service > maxfd)
            maxfd = service;

        struct timeval timeout = {.tv_sec = 0, .tv_usec = 100000};
        int ready = select(maxfd + 1, &readset, NULL, NULL, &timeout);
        if (ready <= 0)
            continue;

        source_len = sizeof(source);
        if (FD_ISSET(command, &readset))
        {
            ssize_t n = recvfrom(command, buffer, sizeof(buffer), 0, (struct sockaddr*)&source,
                                 &source_len);
            if (peer_present && n >= 8 && source.sin_addr.s_addr == gamepad.sin_addr.s_addr)
            {
                uint16_t type = read_le16(buffer), query = read_le16(buffer + 2);
                uint16_t size = read_le16(buffer + 4), seq = read_le16(buffer + 6);
                if (type == 2 && n == 8 + size)
                {
                    last_control = xTaskGetTickCount();
                    ++control_replies;
                    send_command(command, &gamepad, 3, query, seq, NULL, 0);
                    if (query == 0 && size == 12 + 0x304 && buffer[8] == 0x7e && !uic_done)
                    {
                        uic_done = true;
                        ESP_LOGI(TAG, "GamePad control received");
                    }
                    if (query == 1 && size == 16)
                    {
                        bool first_uvc_reply = !uvc_done;
                        uvc_done = true;
                        session_seen = true;
                        uvc[0] = 1;
                        uvc_apply_reply(uvc, buffer + 8);
                        if (first_uvc_reply)
                            ESP_LOGI(TAG, "GamePad video received");
                        video_stream_set_ready(true);
                    }
                }
            }
        }

        source_len = sizeof(source);
        if (FD_ISSET(input, &readset))
        {
            ssize_t n =
                recvfrom(input, buffer, sizeof(buffer), 0, (struct sockaddr*)&source, &source_len);
            if (n > 0 && source.sin_addr.s_addr == gamepad.sin_addr.s_addr)
            {
                last_input = xTaskGetTickCount();
                ++input_packets;
                input_receive(buffer, (size_t)n);
            }
        }

        if (FD_ISSET(message, &readset))
        {
            source_len = sizeof(source);
            ssize_t n = recvfrom(message, buffer, sizeof(buffer), 0, (struct sockaddr*)&source,
                                 &source_len);
            static const uint8_t idr_request[] = {1, 0, 0, 0};
            if (n > 0 && source.sin_addr.s_addr == gamepad.sin_addr.s_addr)
            {
                if (n == sizeof(idr_request) &&
                    memcmp(buffer, idr_request, sizeof(idr_request)) == 0)
                {
                    ++resync_packets;
                    video_stream_request_idr();
                } else
                {
                    ++other_messages;
                }
            }
        }

        if (FD_ISSET(service, &readset))
        {
            source_len = sizeof(source);
            ssize_t n = recvfrom(service, buffer, sizeof(buffer), 0, (struct sockaddr*)&source,
                                 &source_len);
            if (n > 0 && source.sin_addr.s_addr == gamepad.sin_addr.s_addr)
                ++service_packets;
        }
    }
}

void runtime_start(void)
{
    if (xTaskCreate(runtime_task, "gamepad_runtime", 8192, NULL, 5, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Could not start GamePad UDP control task");
    }
}
