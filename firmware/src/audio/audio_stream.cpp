#include "audio_stream.h"
#include "common/wire_limits.h"
#include "video/video_stream.h"

#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include <stdatomic.h>

#define GAMEPAD_AUDIO_PORT 50121
#define ASTRM_HEADER_SIZE 8
// One chunk is 10 ms. Large pictures can hold for ~20 ms, keep 30 ms
#define BUFFER_BYTES (AUDIO_CHUNK_BYTES * 7)
#define CHUNK_MS 10
#define PRIME_CHUNKS 3
#define CATCH_UP_CHUNKS 6

static const char* TAG = "audio_stream";
static StreamBufferHandle_t buffer;
static atomic_bool active;
static atomic_uint dropped, sent, send_failures, underruns;
static atomic_llong vibrate_until_us;
#define MAX_VIBRATE_MS 5000

void audio_stream_set_active(bool value)
{
    atomic_store_explicit(&active, value, memory_order_release);
}

void audio_stream_push(const uint8_t* data, size_t length)
{
    if (!buffer || !atomic_load_explicit(&active, memory_order_acquire))
        return;
    if (xStreamBufferSpacesAvailable(buffer) < length)
    {
        atomic_fetch_add_explicit(&dropped, 1, memory_order_relaxed);
        return;
    }
    xStreamBufferSend(buffer, data, length, 0);
}

void audio_stream_vibrate(uint32_t duration_ms)
{
    // The pad vibrates while astrm packets carry the vibrate bit.
    if (duration_ms > MAX_VIBRATE_MS)
        duration_ms = MAX_VIBRATE_MS;
    atomic_store_explicit(&vibrate_until_us,
                          duration_ms ? esp_timer_get_time() + (int64_t)duration_ms * 1000 : 0,
                          memory_order_release);
}

static bool send_packet(uint8_t* packet, bool vibrate, uint16_t& sequence,
                        const struct sockaddr_in& destination)
{
    const int fd = video_stream_audio_socket();
    if (fd < 0 || !video_stream_gamepad_ready())
        return false;
    const uint32_t timestamp = (uint32_t)esp_wifi_get_tsf_time(WIFI_IF_AP);
    packet[0] = (vibrate ? 8 : 0) | ((sequence >> 8) & 3);
    packet[1] = sequence;
    packet[2] = (AUDIO_CHUNK_BYTES >> 8) & 0xff;
    packet[3] = AUDIO_CHUNK_BYTES & 0xff;
    packet[4] = timestamp;
    packet[5] = timestamp >> 8;
    packet[6] = timestamp >> 16;
    packet[7] = timestamp >> 24;
    sequence = (sequence + 1) & 1023;
    const int result = sendto(fd, packet, ASTRM_HEADER_SIZE + AUDIO_CHUNK_BYTES, 0,
                              (const struct sockaddr*)&destination, sizeof(destination));
    if (result == (int)(ASTRM_HEADER_SIZE + AUDIO_CHUNK_BYTES))
        atomic_fetch_add_explicit(&sent, 1, memory_order_relaxed);
    else
        atomic_fetch_add_explicit(&send_failures, 1, memory_order_relaxed);
    return true;
}

static void audio_sender_task(void*)
{
    uint8_t packet[ASTRM_HEADER_SIZE + AUDIO_CHUNK_BYTES];
    uint16_t sequence = 0;
    bool primed = false;
    unsigned release_packets = 0;
    struct sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(GAMEPAD_AUDIO_PORT);
    destination.sin_addr.s_addr = htonl(0xc0a8010b);
    TickType_t wake = xTaskGetTickCount();
    TickType_t next_stats = wake + pdMS_TO_TICKS(10000);
    while (true)
    {
        // One chunk per 10 ms keeps the pad's input steady even when USB delivers in bursts.
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CHUNK_MS));
        if (!atomic_load_explicit(&active, memory_order_acquire))
        {
            while (xStreamBufferReceive(buffer, packet, sizeof(packet), 0))
                ;
            primed = false;
            continue;
        }
        const bool vibrate = atomic_load_explicit(&vibrate_until_us, memory_order_acquire) >
                             esp_timer_get_time();
        if (vibrate)
            release_packets = 3; 
        size_t level = xStreamBufferBytesAvailable(buffer);
        if (!primed && level >= AUDIO_CHUNK_BYTES * PRIME_CHUNKS)
            primed = true;
        if (primed && level < AUDIO_CHUNK_BYTES)
        {
            primed = false;
            atomic_fetch_add_explicit(&underruns, 1, memory_order_relaxed);
        }
        if (!primed)
        {
            if (release_packets)
            {
                memset(packet + ASTRM_HEADER_SIZE, 0, AUDIO_CHUNK_BYTES);
                send_packet(packet, vibrate, sequence, destination);
                if (!vibrate)
                    --release_packets;
            }
            continue;
        }
        release_packets = 0;
        // The PC and ESP clocks differ slightly
        const unsigned count = level >= AUDIO_CHUNK_BYTES * CATCH_UP_CHUNKS ? 2 : 1;
        for (unsigned i = 0; i < count; ++i)
        {
            if (xStreamBufferReceive(buffer, packet + ASTRM_HEADER_SIZE, AUDIO_CHUNK_BYTES, 0) !=
                AUDIO_CHUNK_BYTES)
                break;
            send_packet(packet, vibrate, sequence, destination);
        }

        if ((int32_t)(xTaskGetTickCount() - next_stats) >= 0)
        {
            ESP_LOGI(TAG, "PC_AUDIO_STATS:sent=%u dropped=%u underruns=%u send_failures=%u",
                     atomic_load_explicit(&sent, memory_order_relaxed),
                     atomic_load_explicit(&dropped, memory_order_relaxed),
                     atomic_load_explicit(&underruns, memory_order_relaxed),
                     atomic_load_explicit(&send_failures, memory_order_relaxed));
            next_stats = xTaskGetTickCount() + pdMS_TO_TICKS(10000);
        }
    }
}

void audio_stream_start(void)
{
    buffer = xStreamBufferCreate(BUFFER_BYTES, 1);
    /* The ESP has one core and the video sender can hold it for ~20 ms per picture. Each audio
     * send takes well under a millisecond, so it runs above video (5) and USB receive (6). */
    if (!buffer || xTaskCreate(audio_sender_task, "audio_tx", 3072, NULL, 7, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Could not start PC audio; the stream will be silent");
        return;
    }
    ESP_LOGI(TAG, "PC audio ready: %u-byte chunks, %u-byte buffer", AUDIO_CHUNK_BYTES,
             BUFFER_BYTES);
}
