#include "video_stream.h"
#include "network/udp.h"
#include "video_packets.h"
#include "send_retry.h"
#include "video_policy.h"
#include "tx_pressure.h"
#include "transport/usb_stream.h"
#include "transport/bridge_link.h"
#include "common/bridge_record.h"
#include "audio/audio_stream.h"
#include "diagnostics/memory_diagnostics.h"
#include "common/drh_record.h"
#include "common/video_profile.h"

#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include <errno.h>
#include <stdatomic.h>
#include <string.h>

#define VIDEO_LOCAL_PORT 50020
#define AUDIO_LOCAL_PORT 50021
#define GAMEPAD_VIDEO_PORT 50120
#define GAMEPAD_AUDIO_PORT 50121
#define CHUNK_COUNT DRH_CHUNK_COUNT
#define SPREAD_PACKET_THRESHOLD 8 /* a P picture is normally 5-6 packets */

static const char* TAG = "video_stream";
static const uint8_t LOOP_MAGIC[16] = "TOFFEE-DRH-LOOP";
static const int64_t CHUNK_OFFSETS_US[CHUNK_COUNT] = {0, 3000, 6000, 9000, 11000};
static int video_socket = -1;
static int audio_socket = -1;

/* State other tasks read or write (USB reception, GamePad control, feedback polling). */
static struct {
    atomic_bool gamepad_ready;
    atomic_uint last_pc_activity_ms;
    atomic_bool video_started; /* the receiver has a reference picture to predict from */
    atomic_bool need_idr;
    atomic_bool recovering;
    atomic_bool standby_active;
    atomic_uint last_idr_notice_ms, repeated_idr_notices;
} shared;

/* Everything below is touched only by video_sender. */

/* The sender's own state, kept across streams. */
static struct {
    uint8_t packet[16 + MAX_VIDEO_PACKET_PAYLOAD];
    uint16_t sequence; /* the 10-bit video packet sequence */
    video_schedule_t schedule;
    int64_t last_failure_log;
} sender;

/* The PC stream in progress: what its header promised and how far it has got. */
static struct {
    bool valid;
    uint32_t expected_frames;
    uint32_t parsed_frames;
    uint32_t record_sequence; /* the picture being sent, or UINT32_MAX for a hold frame */
} stream;

/* Counters for the stream in progress; STREAM_BEGIN clears them all. */
static struct {
    uint32_t sent, skipped, local_failures;
    uint32_t send_retries, retry_recovered, retry_exhausted;
    int64_t retry_wait_max;
    uint32_t format_only_sent, hold_frames_sent;
    int64_t schedule_wait_max, udp_submit_max;
    int64_t last_format_us, format_gap_max;
    int64_t next_report;
    video_tx_pressure_t tx;
} stats;

/* The standby clip. Its last picture changes nothing, so standby repeats it once the clip has
 * played. */
static struct {
    drh_picture_t picture;
    uint32_t frame_count;
    const uint8_t* last_frame;
} standby;
extern const uint8_t standby_asset_start[] asm("_binary_standby_idr_p_drc_start");
extern const uint8_t standby_asset_end[] asm("_binary_standby_idr_p_drc_end");
#ifdef CONFIG_TOFFEE_VIDEO_HOLD_FRAMES
static drh_picture_t hold_picture;
extern const uint8_t hold_asset_start[] asm("_binary_hold_p_drc_start");
extern const uint8_t hold_asset_end[] asm("_binary_hold_p_drc_end");
#endif

static void receive_record(uint8_t type, uint32_t sequence, const uint8_t* payload, size_t length);

#ifdef CONFIG_TOFFEE_VIDEO_HOLD_FRAMES
static bool load_hold_frame(void)
{
    return drh_parse_picture(hold_asset_start, (size_t)(hold_asset_end - hold_asset_start),
                             &hold_picture, NULL) == DRH_RECORD_OK &&
           !hold_picture.idr;
}
#endif

static bool read_standby_frame(const uint8_t** cursor)
{
    size_t consumed;
    if (drh_read_picture(*cursor, (size_t)(standby_asset_end - *cursor), &standby.picture,
                         &consumed, NULL) != DRH_RECORD_OK)
        return false;
    *cursor += consumed;
    return true;
}

static bool validate_standby_asset(void)
{
    const uint8_t* p = standby_asset_start;
    const uint8_t* end = standby_asset_end;
    if (end - p < 32 || memcmp(p, LOOP_MAGIC, sizeof(LOOP_MAGIC)) != 0 ||
        drh_read_u32(p + 16) != VIDEO_WIDTH || drh_read_u32(p + 20) != VIDEO_HEIGHT ||
        drh_read_u32(p + 24) != VIDEO_FPS)
        return false;
    standby.frame_count = drh_read_u32(p + 28);
    p += 32;
    if (!standby.frame_count || standby.frame_count > (size_t)(end - p) / 30)
        return false;
    for (uint32_t frame = 0; frame < standby.frame_count; ++frame)
    {
        standby.last_frame = p;
        if (!read_standby_frame(&p) || (frame == 0 && !standby.picture.idr))
            return false;
    }
    return p == end;
}

static void wait_until_us(int64_t deadline)
{
    int64_t remaining;
    while ((remaining = deadline - esp_timer_get_time()) > 1200)
    {
        TickType_t ticks = pdMS_TO_TICKS((uint32_t)(remaining / 1000) - 1);
        if (ticks)
            vTaskDelay(ticks);
    }
    remaining = deadline - esp_timer_get_time();
    if (remaining > 0)
        esp_rom_delay_us((uint32_t)remaining);
}

static void report_send_failure(bool pc, const char* part, int error)
{
    const int64_t now = esp_timer_get_time();
    if (!sender.last_failure_log || now - sender.last_failure_log >= 1000000)
    {
        ESP_LOGW(
            TAG,
            "%s send failed: errno=%d " HEAP_STATS_FMT, part, error, HEAP_STATS_ARGS);
        if (pc)
            ESP_LOGW(TAG,
                     "PC_TX_FAILURE:seq=%lu idr=%u picture_bytes=%lu accepted_packets=%lu "
                     "accepted_bytes=%lu elapsed_us=%lld budget_left_us=%lld",
                     (unsigned long)stats.tx.sequence, (unsigned)stats.tx.idr,
                     (unsigned long)stats.tx.picture_bytes,
                     (unsigned long)stats.tx.accepted_packets,
                     (unsigned long)stats.tx.accepted_bytes,
                     (long long)(now - stats.tx.started_us),
                     (long long)stats.tx.budget_left_us);
        sender.last_failure_log = now;
    }
}

typedef struct {
    int fd, size;
    const void* data;
    const struct sockaddr_in* destination;
} send_context_t;
static video_send_result_t socket_submit(void* opaque)
{
    auto* context = static_cast<send_context_t*>(opaque);
    const uint32_t allocations_before = memory_diagnostics_failure_count();
    const int sent =
        sendto(context->fd, context->data, context->size, 0,
               (const struct sockaddr*)context->destination, sizeof(*context->destination));
    const int error = sent < 0 ? errno : 0;
    if (sent < 0)
        memory_diagnostics_rejected((unsigned)context->size, error, allocations_before);
    video_send_result_t result{};
    result.bytes = sent;
    result.error = error;
    return result;
}
static int64_t retry_clock(void* unused)
{
    (void)unused;
    return esp_timer_get_time();
}
static void retry_pause(void* unused)
{
    (void)unused;
    vTaskDelay(1);
}
static bool send_datagram(int fd, const void* data, int size, const struct sockaddr_in* destination,
                          int64_t* budget, bool pc)
{
    send_context_t context = {fd, size, data, destination};
    const video_send_result_t result =
        video_send_with_retry(&context, size, budget, socket_submit, retry_clock, retry_pause);
    if (pc)
    {
        stats.tx.budget_left_us = *budget;
        if (result.bytes == size)
            video_tx_accepted(&stats.tx, (uint32_t)size);
        stats.tx.rejected += result.retries + (result.bytes == size ? 0 : 1);
        stats.send_retries += result.retries;
        if (result.waited_us > stats.retry_wait_max)
            stats.retry_wait_max = result.waited_us;
        if (result.bytes == size && result.retries)
            ++stats.retry_recovered;
        if (result.bytes == -1 && video_send_transient(result.error))
            ++stats.retry_exhausted;
    }
    if (result.bytes == size)
        return true;
    errno = result.error;
    return false;
}

static void apply_recovery(video_recovery_event_t event, bool pc)
{
    const video_recovery_effect_t effect = video_recovery_effect(event, pc);
    if (event == VIDEO_BEGIN_IDR)
    {
        atomic_store_explicit(&shared.recovering, effect.recovering != 0, memory_order_release);
        atomic_store_explicit(&shared.need_idr, effect.need_idr != 0, memory_order_release);
        return;
    }
    if (effect.started >= 0)
        atomic_store_explicit(&shared.video_started, effect.started != 0, memory_order_release);
    if (effect.need_idr >= 0)
        atomic_store_explicit(&shared.need_idr, effect.need_idr != 0, memory_order_release);
    if (effect.recovering >= 0)
        atomic_store_explicit(&shared.recovering, effect.recovering != 0, memory_order_release);
}

static bool submit_frame(bool pc, bool idr, const uint8_t* payloads[CHUNK_COUNT],
                         const uint16_t sizes[CHUNK_COUNT])
{
    if (video_socket < 0 || audio_socket < 0 ||
        !atomic_load_explicit(&shared.gamepad_ready, memory_order_acquire))
        return false;
    if (idr)
        apply_recovery(VIDEO_BEGIN_IDR, pc);

    const int64_t now = esp_timer_get_time();
    const int64_t frame_slot = video_schedule_slot(&sender.schedule, now);
    if (now < frame_slot)
        wait_until_us(frame_slot);
    const int64_t published = esp_timer_get_time();
    if (pc)
    {
        uint32_t picture_bytes = payloads ? 25 : 0;
        if (payloads)
            for (unsigned i = 0; i < CHUNK_COUNT; ++i)
                picture_bytes += sizes[i];
        video_tx_begin(&stats.tx, payloads ? stream.record_sequence : UINT32_MAX, picture_bytes, idr,
                       published);
    }
    if (pc && published - now > stats.schedule_wait_max)
        stats.schedule_wait_max = published - now;
    // Keep failed frames paced too: retrying immediately makes buffer pressure worse.
    video_schedule_advance(&sender.schedule);

    const int64_t tsf_before_us = esp_timer_get_time();
    const int64_t tsf = esp_wifi_get_tsf_time(WIFI_IF_AP);
    const int64_t tsf_after_us = esp_timer_get_time();
    uint32_t timestamp = (uint32_t)(tsf > 1250 ? tsf - 1250 : 0);
    if (pc)
    {
        timestamp = video_pc_timestamp(tsf, tsf_before_us, tsf_after_us, frame_slot);
    }
    uint8_t format[32];
    /* Only explicitly rejected PC datagrams are retried, so a reference chain is not abandoned at
     * the first Wi-Fi TX buffer shortage. Buffers free up as earlier packets go out, about 0.5 ms
     * each on the GamePad link; a dropped picture costs a recovery IDR, which is far worse than
     * a frame that leaves a few milliseconds late. */
    int64_t retry_budget = pc ? 8000 : 0;
    if (pc)
        video_format(format, timestamp);
    else
        standby_format(format, timestamp);
    struct sockaddr_in audio_destination{};
    audio_destination.sin_family = AF_INET;
    audio_destination.sin_port = htons(GAMEPAD_AUDIO_PORT);
    audio_destination.sin_addr.s_addr = htonl(0xc0a8010b);
    if (!send_datagram(audio_socket, format, sizeof(format), &audio_destination, &retry_budget, pc))
    {
        report_send_failure(pc, "format", errno);
        return false;
    }
    if (pc)
    {
        if (stats.last_format_us && published - stats.last_format_us > stats.format_gap_max)
            stats.format_gap_max = published - stats.last_format_us;
        stats.last_format_us = published;
    }
    // An empty slot keeps the format clock running without sending video
    if (!payloads)
    {
        if (pc)
            ++stats.format_only_sent;
        return true;
    }

    struct sockaddr_in video_destination{};
    video_destination.sin_family = AF_INET;
    video_destination.sin_port = htons(GAMEPAD_VIDEO_PORT);
    video_destination.sin_addr.s_addr = htonl(0xc0a8010b);
    uint8_t* packet = sender.packet;
    /* Large PC pictures (mostly IDRs) are spaced over the frame like standby video, so a burst
     * does not exhaust the Wi-Fi TX buffers. Small pictures keep their low latency. */
    unsigned packet_total = 0;
    for (unsigned index = 0; index < CHUNK_COUNT; ++index)
        packet_total += (sizes[index] + MAX_VIDEO_PACKET_PAYLOAD - 1) / MAX_VIDEO_PACKET_PAYLOAD;
    const bool spread = pc && packet_total > SPREAD_PACKET_THRESHOLD;
    for (unsigned index = 0; index < CHUNK_COUNT; ++index)
    {
        if (!pc)
            wait_until_us(published + 5000 + CHUNK_OFFSETS_US[index]);
        else if (spread)
            wait_until_us(published + CHUNK_OFFSETS_US[index]);
        const bool init = pc ? stats.sent == 0 : idr;
        uint16_t offset = 0;
        while (offset < sizes[index])
        {
            const uint16_t size = vstrm_fragment(packet, sender.sequence, timestamp, payloads[index],
                                                 sizes[index], offset, index, idr, init);
            if (!send_datagram(video_socket, packet, 16 + size, &video_destination, &retry_budget,
                               pc))
            {
                report_send_failure(pc, "video", errno);
                return false;
            }
            offset += size;
            sender.sequence = (sender.sequence + 1) & 1023;
        }
    }

    apply_recovery(idr ? VIDEO_SUBMITTED_IDR : VIDEO_SUBMITTED_P, pc);
    if (pc)
    {
        const int64_t submit_us = esp_timer_get_time() - published;
        if (submit_us > stats.udp_submit_max)
            stats.udp_submit_max = submit_us;
    }
    return true;
}

static bool submit_checked_frame(bool pc, bool idr, const uint8_t* payloads[CHUNK_COUNT],
                                 const uint16_t sizes[CHUNK_COUNT])
{
    bool sent = submit_frame(pc, idr, payloads, sizes);
    /* Keep the tested standby format-only slot after each IDR. */
    if (sent && idr && !pc)
        sent = submit_frame(pc, false, NULL, NULL);
    if (!sent)
    {
        if (pc)
            ++stats.local_failures;
        apply_recovery(VIDEO_LOCAL_FAILURE, pc);
        /* A local loss invalidates following P pictures. Notify the host even
         * though need_idr is already set, rather than waiting for its timer. */
        if (pc)
        {
            atomic_store_explicit(&shared.last_idr_notice_ms, (uint32_t)(esp_timer_get_time() / 1000),
                                  memory_order_release);
            ESP_LOGI(TAG, "PC_IDR_REQUEST");
            bridge_send(BRIDGE_IDR_REQUEST, 0, NULL, 0);
        }
    }
    return sent;
}

static bool send_video_frame(bool idr, const uint8_t* payloads[CHUNK_COUNT],
                             const uint16_t sizes[CHUNK_COUNT])
{
    bool sent;
    /* Keep P pictures ordered during receiver-requested recovery. A local
     * failure or reconnect still requires an IDR before dependent pictures. */
    if (!video_recovery_can_submit(idr, atomic_load_explicit(&shared.video_started, memory_order_acquire)))
    {
        ++stats.skipped;
        submit_frame(true, false, NULL, NULL);
        sent = false;
    } else
    {
        sent = submit_checked_frame(true, idr, payloads, sizes);
        if (sent)
            ++stats.sent;
    }
    return sent;
}

static void video_sender_task(void* unused)
{
    (void)unused;
    const uint8_t* cursor = standby_asset_start + 32;
    uint32_t frame = 0;
    while (true)
    {
        usb_record_t record;
        if (usb_stream_take(&record, 0))
        {
            receive_record(record.type, record.sequence, record.payload, record.length);
            usb_stream_complete(&record);
            continue;
        }
        bool sending = false;
        const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        const uint32_t last_pc_ms =
            atomic_load_explicit(&shared.last_pc_activity_ms, memory_order_acquire);
        const bool pc_active = video_pc_active(now_ms, last_pc_ms);
        const video_idle_action_t action =
            video_idle_action(atomic_load_explicit(&shared.gamepad_ready, memory_order_acquire), pc_active,
                              sender.schedule.next_deadline, esp_timer_get_time());
        if (action == VIDEO_ACTION_STANDBY)
        {
            sending = true;
            if (!atomic_exchange_explicit(&shared.standby_active, true, memory_order_acq_rel))
            {
                cursor = standby_asset_start + 32;
                frame = 0;
                ESP_LOGI(TAG, "PC video idle; showing standby IDR/P loop");
            }
            if (atomic_load_explicit(&shared.need_idr, memory_order_acquire))
            {
                cursor = standby_asset_start + 32;
                frame = 0;
            }
            if (read_standby_frame(&cursor) &&
                submit_checked_frame(false, standby.picture.idr, standby.picture.chunks,
                                     standby.picture.sizes))
            {
                // Looping the clip redrew the logo every two seconds; stay on its last, unchanging
                // picture until the GamePad asks for a fresh one.
                if (++frame == standby.frame_count)
                {
                    cursor = standby.last_frame;
                    --frame;
                }
            } else
            {
                cursor = standby_asset_start + 32;
                frame = 0;
            }
        } else
        {
            if (atomic_exchange_explicit(&shared.standby_active, false, memory_order_acq_rel) && pc_active)
                ESP_LOGI(TAG, "PC video received; leaving standby loop");
        }
        /* Standby's last chunk is at +16 ms on a 16.683-ms clock. Even a
         * one-tick queue wait before its next frame would disturb cadence.
         * Block only while no picture is being sent, waking on ingress. */
        if (!sending)
        {
            if (video_idle_action(atomic_load_explicit(&shared.gamepad_ready, memory_order_acquire),
                                  pc_active, sender.schedule.next_deadline,
                                  esp_timer_get_time()) == VIDEO_ACTION_FORMAT_ONLY)
            {
                // USB may still be receiving a large picture. Maintain the
                // format clock while retaining the previous displayed image.
#ifdef CONFIG_TOFFEE_VIDEO_HOLD_FRAMES
                if (video_hold_allowed(atomic_load_explicit(&shared.video_started, memory_order_acquire),
                                       atomic_load_explicit(&shared.need_idr, memory_order_acquire)))
                {
                    const uint32_t saved_sequence = stream.record_sequence;
                    stream.record_sequence = UINT32_MAX;
                    if (submit_checked_frame(true, false, hold_picture.chunks, hold_picture.sizes))
                        ++stats.hold_frames_sent;
                    stream.record_sequence = saved_sequence;
                } else
#endif
                    submit_frame(true, false, NULL, NULL);
            } else if (usb_stream_take(&record, 1))
            {
                receive_record(record.type, record.sequence, record.payload, record.length);
                usb_stream_complete(&record);
            }
        }
    }
}

esp_err_t video_stream_start(void)
{
#ifdef CONFIG_TOFFEE_VIDEO_HOLD_FRAMES
    if (!load_hold_frame())
    {
        ESP_LOGE(TAG, "Identity P asset is invalid");
        return ESP_ERR_INVALID_SIZE;
    }
#endif
    if (!validate_standby_asset())
    {
        ESP_LOGE(TAG, "Embedded standby frame is invalid");
        return ESP_ERR_INVALID_SIZE;
    }
    video_socket = udp_bind(VIDEO_LOCAL_PORT);
    audio_socket = udp_bind(AUDIO_LOCAL_PORT);
    if (video_socket < 0 || audio_socket < 0)
    {
        if (video_socket >= 0)
            close(video_socket);
        if (audio_socket >= 0)
            close(audio_socket);
        video_socket = audio_socket = -1;
        return ESP_FAIL;
    }
    atomic_store(&shared.gamepad_ready, false);
    atomic_store(&shared.last_pc_activity_ms, 0);
    stream.valid = false;
    atomic_store_explicit(&shared.video_started, false, memory_order_release);
    atomic_store_explicit(&shared.need_idr, true, memory_order_release);
    stream.parsed_frames = 0;
    sender.sequence = 0;
    sender.schedule.next_deadline = 0;
    ESP_LOGI(TAG, "GamePad video UDP ready: local %u/%u -> pad %u/%u", VIDEO_LOCAL_PORT,
             AUDIO_LOCAL_PORT, GAMEPAD_VIDEO_PORT, GAMEPAD_AUDIO_PORT);
    if (xTaskCreate(video_sender_task, "video_sender", 6144, NULL, 5, NULL) != pdPASS)
    {
        close(video_socket);
        close(audio_socket);
        video_socket = audio_socket = -1;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void video_stream_set_ready(bool ready)
{
    if (!ready)
        apply_recovery(VIDEO_READY_LOST, false);
    if (ready && !atomic_load_explicit(&shared.gamepad_ready, memory_order_acquire))
        apply_recovery(VIDEO_READY_GAINED, false);
    const bool previous = atomic_exchange_explicit(&shared.gamepad_ready, ready, memory_order_acq_rel);
    if (!ready && previous)
        usb_stream_finish_session();
    if (ready && !previous)
        ESP_LOGI(TAG, "GamePad video control is ready");
    if (ready != previous)
        bridge_send_state();
}

int video_stream_audio_socket(void) { return audio_socket; }

bool video_stream_gamepad_ready(void)
{
    return atomic_load_explicit(&shared.gamepad_ready, memory_order_acquire);
}

void video_stream_poll_feedback(void)
{
    const int sockets[] = {video_socket, audio_socket};
    uint8_t buffer[64];
    for (unsigned i = 0; i < 2; ++i)
    {
        if (sockets[i] < 0)
            continue;
        // Unread datagrams keep the driver's zero-copy receive buffers occupied.
        for (unsigned count = 0; count < 8; ++count)
        {
            ssize_t n = recv(sockets[i], buffer, sizeof(buffer), MSG_DONTWAIT);
            if (n < 0)
                break;
        }
    }
}

void video_stream_request_idr(void)
{
    const uint32_t last_pc = atomic_load_explicit(&shared.last_pc_activity_ms, memory_order_acquire);
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    const bool pc_active = video_pc_active(now, last_pc);
    if (video_recovery_accept_request(pc_active,
                                      atomic_load_explicit(&shared.recovering, memory_order_acquire)))
    {
        const bool already_pending =
            atomic_exchange_explicit(&shared.need_idr, true, memory_order_acq_rel);
        const uint32_t last_notice =
            atomic_load_explicit(&shared.last_idr_notice_ms, memory_order_acquire);
        if (pc_active && video_recovery_notify(already_pending, now, last_notice))
        {
            atomic_store_explicit(&shared.last_idr_notice_ms, now, memory_order_release);
            if (already_pending)
                atomic_fetch_add_explicit(&shared.repeated_idr_notices, 1, memory_order_relaxed);
            ESP_LOGI(TAG, "PC_IDR_REQUEST");
            bridge_send(BRIDGE_IDR_REQUEST, 0, NULL, 0);
        }
    }
}

static void begin_pc_stream(uint32_t sequence, const uint8_t* payload, size_t length)
{
    audio_stream_set_active(false);
    const bool valid = sequence == VIDEO_STREAM_PROFILE && length == 32 &&
                       memcmp(payload, LOOP_MAGIC, sizeof(LOOP_MAGIC)) == 0 &&
                       drh_read_u32(payload + 16) == VIDEO_WIDTH &&
                       drh_read_u32(payload + 20) == VIDEO_HEIGHT &&
                       drh_read_u32(payload + 24) == VIDEO_FPS && drh_read_u32(payload + 28) > 0;
    if (!valid)
    {
        stream.valid = false;
        bridge_send_error("stream header rejected");
        return;
    }
    if (!usb_stream_prepare_session())
    {
        stream.valid = false;
        atomic_store_explicit(&shared.last_pc_activity_ms, 0, memory_order_release);
        return;
    }
    stream.expected_frames = drh_read_u32(payload + 28);
    stream.parsed_frames = 0;
    stream.valid = true;
    atomic_store_explicit(&shared.last_pc_activity_ms, (uint32_t)(esp_timer_get_time() / 1000),
                          memory_order_release);
    memset(&stats, 0, sizeof(stats));
    stats.next_report = esp_timer_get_time() + 1000000;
    atomic_store_explicit(&shared.last_idr_notice_ms, 0, memory_order_release);
    atomic_store_explicit(&shared.repeated_idr_notices, 0, memory_order_relaxed);
    atomic_store_explicit(&shared.video_started, false, memory_order_release);
    atomic_store_explicit(&shared.need_idr, true, memory_order_release);
    sender.schedule.next_deadline = esp_timer_get_time();
    if (atomic_exchange_explicit(&shared.standby_active, false, memory_order_acq_rel))
    {
        ESP_LOGI(TAG, "PC video stream started");
    }
    ESP_LOGD(TAG, "USB video stream started: %lu frames at %ux%u/%u",
             (unsigned long)stream.expected_frames, VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_FPS);
    audio_stream_set_active(true);
    ESP_LOGI(TAG, "PC_STREAM_READY:%lu", (unsigned long)sequence);
    bridge_send(BRIDGE_STREAM_READY, sequence, NULL, 0);
}

static void end_pc_stream(void)
{
    audio_stream_set_active(false);
    if (stream.valid && stream.expected_frames != UINT32_MAX && stream.parsed_frames != stream.expected_frames)
        ESP_LOGW(TAG, "USB stream ended after %lu/%lu frames", (unsigned long)stream.parsed_frames,
                 (unsigned long)stream.expected_frames);
    else
        ESP_LOGD(TAG, "USB video stream ended: %lu frames", (unsigned long)stream.parsed_frames);
    ESP_LOGI(TAG, "USB video ended: records=%lu sent=%lu skipped=%lu", (unsigned long)stream.parsed_frames,
             (unsigned long)stats.sent, (unsigned long)stats.skipped);
    ESP_LOGI(
        TAG,
        "PC_TX_RESULT:sent=%lu skipped=%lu failed=%lu rejected=%lu retries=%lu retry_recovered=%lu "
        "retry_exhausted=%lu idr_frames=%lu p_frames=%lu idr_packets_max=%lu p_packets_max=%lu "
        "clock_gap_max_us=%lld udp_max_us=%lld hold_frames=%lu",
        (unsigned long)stats.sent, (unsigned long)stats.skipped,
        (unsigned long)stats.local_failures, (unsigned long)stats.tx.rejected,
        (unsigned long)stats.send_retries, (unsigned long)stats.retry_recovered, (unsigned long)stats.retry_exhausted,
        (unsigned long)stats.tx.idr_frames, (unsigned long)stats.tx.p_frames,
        (unsigned long)stats.tx.idr_packets_max, (unsigned long)stats.tx.p_packets_max,
        (long long)stats.format_gap_max, (long long)stats.udp_submit_max, (unsigned long)stats.hold_frames_sent);
    stream.valid = false;
    atomic_store_explicit(&shared.last_pc_activity_ms, 0, memory_order_release);
    usb_stream_finish_session();
}

static void log_pc_video_stats(void)
{
    const int64_t now = esp_timer_get_time();
    if (now >= stats.next_report)
    {
        ESP_LOGI(TAG,
                 "PC_VIDEO_STATS:sent=%lu skipped=%lu failed=%lu schedule_max_us=%lld "
                 "udp_max_us=%lld clock_only=%lu clock_gap_max_us=%lld retries=%lu "
                 "retry_recovered=%lu retry_exhausted=%lu retry_wait_max_us=%lld idr_pending=%u "
                 "recovering=%u reference_started=%u notices_repeated=%u",
                 (unsigned long)stats.sent, (unsigned long)stats.skipped,
                 (unsigned long)stats.local_failures, (long long)stats.schedule_wait_max,
                 (long long)stats.udp_submit_max, (unsigned long)stats.format_only_sent,
                 (long long)stats.format_gap_max, (unsigned long)stats.send_retries,
                 (unsigned long)stats.retry_recovered, (unsigned long)stats.retry_exhausted,
                 (long long)stats.retry_wait_max,
                 (unsigned)atomic_load_explicit(&shared.need_idr, memory_order_acquire),
                 (unsigned)atomic_load_explicit(&shared.recovering, memory_order_acquire),
                 (unsigned)atomic_load_explicit(&shared.video_started, memory_order_acquire),
                 (unsigned)atomic_load_explicit(&shared.repeated_idr_notices, memory_order_relaxed));
        ESP_LOGI(TAG,
                 "PC_TX_PRESSURE:idr_frames=%lu p_frames=%lu idr_packets_max=%lu p_packets_max=%lu "
                 "rejected=%lu hold_frames=%lu",
                 (unsigned long)stats.tx.idr_frames, (unsigned long)stats.tx.p_frames,
                 (unsigned long)stats.tx.idr_packets_max,
                 (unsigned long)stats.tx.p_packets_max, (unsigned long)stats.tx.rejected,
                 (unsigned long)stats.hold_frames_sent);
        stats.next_report = now + 1000000;
    }
}

static void receive_pc_picture(uint32_t sequence, const uint8_t* payload, size_t length)
{

    drh_picture_t picture;
    unsigned invalid_chunk = 0;
    const drh_record_error_t error = drh_parse_picture(payload, length, &picture, &invalid_chunk);
    if (sequence != stream.parsed_frames || sequence >= stream.expected_frames ||
        error == DRH_RECORD_HEADER_INVALID || (sequence == 0 && payload[0] == 0))
    {
        ESP_LOGE(TAG, "USB video frame %lu header invalid", (unsigned long)sequence);
        bridge_send_error("video frame header invalid");
        stream.valid = false;
        return;
    }
    if (error != DRH_RECORD_OK)
    {
        stream.valid = false;
        bridge_send_error(error == DRH_RECORD_CHUNK_INVALID ? "video frame chunk invalid"
                                                            : "video frame malformed");
        if (error == DRH_RECORD_CHUNK_INVALID)
            ESP_LOGE(TAG, "USB video frame %lu chunk %u invalid", (unsigned long)sequence,
                     invalid_chunk);
        else if (error == DRH_RECORD_TRAILING_BYTES)
            ESP_LOGE(TAG, "USB video frame %lu has trailing bytes", (unsigned long)sequence);
        return;
    }
    ++stream.parsed_frames;
    atomic_store_explicit(&shared.last_pc_activity_ms, (uint32_t)(esp_timer_get_time() / 1000),
                          memory_order_release);
    if (!atomic_load_explicit(&shared.gamepad_ready, memory_order_acquire))
    {
        ++stats.skipped;
        if (sequence == 0)
            ESP_LOGW(TAG, "GamePad not ready; ignoring frames until control is active");
        return;
    }
    stream.record_sequence = sequence;
    const bool sent = send_video_frame(picture.idr, picture.chunks, picture.sizes);
    if (sent && picture.idr)
    {
        // Local submission, not a GamePad recovery-success
        ESP_LOGI(TAG, "PC_IDR_SENT:%lu", (unsigned long)sequence);
        bridge_send(BRIDGE_IDR_SENT, sequence, NULL, 0);
    }
    log_pc_video_stats();
}

static void receive_record(uint8_t type, uint32_t sequence, const uint8_t* payload, size_t length)
{
    if (type == STREAM_BEGIN)
        begin_pc_stream(sequence, payload, length);
    else if (type == STREAM_END)
        end_pc_stream();
    else if (type == VIDEO_FRAME && stream.valid)
        receive_pc_picture(sequence, payload, length);
}
