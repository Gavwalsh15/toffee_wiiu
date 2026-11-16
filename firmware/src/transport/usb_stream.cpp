#include "usb_stream.h"
#include "record_parser.h"
#include "large_buffer.h"
#include "picture_ring.h"
#include "bridge_link.h"
#include "audio/audio_stream.h"
#include "network/connection.h"
#include "diagnostics/memory_diagnostics.h"
#include "common/bridge_record.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdlib.h>

#define USB_RX_BUFFER_SIZE 8192
#define USB_TX_BUFFER_SIZE 8192
#define READ_BUFFER_SIZE 1024
#define SMALL_PAYLOAD_SIZE 4096
/* The PC sends a record without pauses, so silence part-way through one means the rest was never
 * sent, for example a write cancelled as the PC stopped a stream. */
#define ABANDON_IDLE_READS 25 /* 20 ms reads: half a second */

static const char* TAG = "usb_stream";
/* Defined by the patched USB driver (patches/esp-idf-usb-backpressure.patch): it counts RX pauses,
 * never dropped packets. */
extern "C" uint32_t usb_rx_pause_count(void);
typedef struct {
    uint8_t small[SMALL_PAYLOAD_SIZE];
    int64_t received_at;
    usb_record_t record;
} record_slot_t;

static record_slot_t slots[USB_RECORD_SLOTS];
static QueueHandle_t free_slots, ready_records;
/* Large pictures share the session's 64 KiB buffer back to back, so up to USB_RECORD_SLOTS can be
 * resident at once; a worst-case IDR still fits alone. ring_space is given whenever one is freed. */
static picture_ring_t ring;
static_assert(USB_RECORD_SLOTS <= PICTURE_RING_ITEMS, "every slot may hold a large picture");
static SemaphoreHandle_t ring_space;
static SemaphoreHandle_t buffer_mutex;
static uint32_t large_allocations, allocation_failures;
static uint8_t* allocate_large(void* context, size_t size)
{
    (void)context;
    auto* data = static_cast<uint8_t*>(malloc(size));
    if (data)
        ++large_allocations;
    else
        ++allocation_failures;
    return data;
}
static void deallocate_large(void* context, uint8_t* data)
{
    (void)context;
    free(data);
}
static large_buffer_t large_buffer = {
    .allocate = allocate_large,
    .deallocate = deallocate_large,
};
static record_slot_t* receiving;
static uint64_t frames, bytes;
static uint64_t receive_total, reserve_total, crc_total, queue_total, dispatch_total;
static uint32_t largest_picture;
static int64_t rx_max, reserve_max, validate_max, queue_max, dispatch_max, next_stats;

static void release_slot(record_slot_t* slot)
{
    if (slot->record.length > SMALL_PAYLOAD_SIZE && slot->record.payload)
    {
        xSemaphoreTake(buffer_mutex, portMAX_DELAY);
        if (!picture_ring_free(&ring, (size_t)(slot->record.payload - large_buffer.data)))
            abort();
        if (!ring.count)
            large_buffer_release(&large_buffer);
        xSemaphoreGive(buffer_mutex);
        xSemaphoreGive(ring_space);
    }
    slot->record.payload = NULL;
    if (xQueueSend(free_slots, &slot, 0) != pdTRUE)
        abort();
}

/* Audio records are small and consumed immediately, so they bypass the picture slots. */
static uint8_t audio_payload[ADPCM_BLOCK_BYTES];
static bool receiving_audio;
static uint8_t receiving_audio_type;
static uint32_t receiving_audio_length;

static uint8_t* reserve_record(void* unused, uint8_t type, uint32_t sequence, uint32_t length)
{
    (void)unused;
    receiving_audio = type == AUDIO_DATA_ADPCM || type == VIBRATE || type == PAIR_RESET;
    if (receiving_audio)
    {
        receiving_audio_type = type;
        receiving_audio_length = length;
        return audio_payload;
    }
    const int64_t started = esp_timer_get_time();
    xQueueReceive(free_slots, &receiving, portMAX_DELAY);
    uint8_t* payload;
    if (length > SMALL_PAYLOAD_SIZE)
    {
        payload = NULL;
        while (true)
        {
            xSemaphoreTake(buffer_mutex, portMAX_DELAY);
            // Large pictures arrive only inside a session, which retains the buffer.
            const bool usable = large_buffer.retained;
            const long at = usable ? picture_ring_place(&ring, length) : -1;
            if (at >= 0)
            {
                if (ring.count == 1)
                    large_buffer_acquire(&large_buffer, length);
                payload = large_buffer.data + at;
            }
            xSemaphoreGive(buffer_mutex);
            if (payload || !usable)
                break;
            xSemaphoreTake(ring_space, portMAX_DELAY);
        }
    } else
    {
        payload = receiving->small;
    }
    receiving->received_at = started;
    receiving->record = (usb_record_t){
        .type = type,
        .sequence = sequence,
        .payload = payload,
        .length = length,
        .reserve_us = esp_timer_get_time() - started,
        .storage = receiving,
    };
    return payload;
}

static void finish_record(void* unused, uint32_t expected_crc)
{
    (void)unused;
    if (receiving_audio)
    {
        receiving_audio = false;
        if (esp_rom_crc32_le(0, audio_payload, receiving_audio_length) != expected_crc)
            return;
        if (receiving_audio_type == PAIR_RESET)
        {
            if (record_u32(audio_payload) == PAIR_RESET_MAGIC)
                forget_gamepad_and_restart(audio_payload[4]);
        } else if (receiving_audio_type == VIBRATE)
            audio_stream_vibrate(record_u32(audio_payload));
        else
        {
            static uint8_t pcm[AUDIO_CHUNK_BYTES];
            if (adpcm_decode_block(audio_payload, pcm))
                audio_stream_push(pcm, sizeof(pcm));
        }
        return;
    }
    record_slot_t* slot = receiving;
    usb_record_t* record = &slot->record;
    record->receive_us = esp_timer_get_time() - slot->received_at;
    if (!record->payload)
    {
        ESP_LOGE(TAG, "PC_BUFFER_ALLOCATION_FAILED:requested=%u free=%u largest=%u",
                 (unsigned)record->length, (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        bridge_send_error("Not enough bridge memory for this picture");
        release_slot(slot);
        return;
    }
    const int64_t check_at = esp_timer_get_time();
    const uint32_t crc = esp_rom_crc32_le(0, record->payload, record->length);
    record->validate_us = esp_timer_get_time() - check_at;
    if (crc != expected_crc)
    {
        bridge_send_error("USB picture checksum mismatch");
        release_slot(slot);
        return;
    }
    record->queued_at = esp_timer_get_time();
    /* The slots bound receiving + queued + sending, regardless of host input. */
    if (xQueueSend(ready_records, record, 0) != pdTRUE)
        abort();
}

// Drops a record the PC stopped sending part-way, so the parser can find the next one. Without
// this it would count the PC's next records as the missing payload.
static void abandon_record(record_parser_t* parser)
{
    if (!parser->header_used && !parser->magic_used)
        return;
    if (parser->header_used == sizeof(parser->header))
    {
        ESP_LOGW(TAG, "PC_USB_RX_ABANDONED:type=%u sequence=%lu length=%lu received=%u",
                 (unsigned)parser->type, (unsigned long)parser->sequence,
                 (unsigned long)parser->length, (unsigned)parser->payload_used);
        if (receiving_audio)
            receiving_audio = false;
        else
            release_slot(receiving);
    }
    record_reset(parser);
}

bool usb_stream_prepare_session(void)
{
    if (!buffer_mutex)
        return false;
    xSemaphoreTake(buffer_mutex, portMAX_DELAY);
    const bool ready = large_buffer_retain(&large_buffer, MAX_USB_PAYLOAD);
    if (ready && !ring.count)
        picture_ring_reset(&ring, MAX_USB_PAYLOAD);
    xSemaphoreGive(buffer_mutex);
    if (!ready)
    {
        bridge_send_error("Cannot reserve the 64 KiB picture buffer at stream start");
        ESP_LOGE(TAG, "PC_BUFFER_RESERVE_FAILED:free=%u largest=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    } else
    {
        ESP_LOGI(
            TAG,
            "PC_BUFFER_READY:capacity=%u " HEAP_STATS_FMT, MAX_USB_PAYLOAD, HEAP_STATS_ARGS);
    }
    return ready;
}

void usb_stream_finish_session(void)
{
    if (!buffer_mutex)
        return;
    xSemaphoreTake(buffer_mutex, portMAX_DELAY);
    large_buffer_end(&large_buffer);
    xSemaphoreGive(buffer_mutex);
}

bool usb_stream_take(usb_record_t* record, uint32_t timeout_ms)
{
    if (!ready_records || xQueueReceive(ready_records, record, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
        return false;
    record->dispatched_at = esp_timer_get_time();
    return true;
}

static void update_max(int64_t* maximum, int64_t value)
{
    if (value > *maximum)
        *maximum = value;
}

void usb_stream_complete(usb_record_t* record)
{
    const int64_t now = esp_timer_get_time();
    if (record->type == STREAM_BEGIN)
    {
        frames = bytes = 0;
        receive_total = reserve_total = crc_total = queue_total = dispatch_total = 0;
        largest_picture = 0;
        rx_max = reserve_max = validate_max = queue_max = dispatch_max = 0;
        next_stats = now + 1000000;
    }
    if (record->type == VIDEO_FRAME)
    {
        ++frames;
        bytes += record->length;
        receive_total += record->receive_us - record->reserve_us;
        reserve_total += record->reserve_us;
        crc_total += record->validate_us;
        queue_total += record->dispatched_at - record->queued_at;
        dispatch_total += now - record->dispatched_at;
        if (record->length > largest_picture)
            largest_picture = record->length;
        update_max(&rx_max, record->receive_us);
        update_max(&reserve_max, record->reserve_us);
        update_max(&validate_max, record->validate_us);
        update_max(&queue_max, record->dispatched_at - record->queued_at);
        update_max(&dispatch_max, now - record->dispatched_at);
    }
    /* Return memory/credits only after the consumer has stopped using them. */
    release_slot((record_slot_t*)record->storage);
    record->storage = NULL;
    if (record->type == VIDEO_FRAME)
        bridge_send(BRIDGE_FRAME_DONE, record->sequence, NULL, 0);
    if (frames && (now >= next_stats || record->type == STREAM_END))
    {
        xSemaphoreTake(buffer_mutex, portMAX_DELAY);
        const unsigned buffer_capacity = large_buffer.capacity;
        const bool buffer_retained = large_buffer.retained;
        const uint32_t allocations = large_allocations, failures = allocation_failures;
        xSemaphoreGive(buffer_mutex);
        ESP_LOGI(TAG,
                 "PC_PIPELINE_STATS:frames=%llu bytes=%llu rx_max_us=%lld reserve_max_us=%lld "
                 "crc_max_us=%lld queue_max_us=%lld dispatch_max_us=%lld free=%u min=%u largest=%u "
                 "rx_pauses=%lu tx_dropped=%lu "
                 "buffer_capacity=%u buffer_retained=%u large_allocs=%lu alloc_failures=%lu",
                 (unsigned long long)frames, (unsigned long long)bytes, (long long)rx_max,
                 (long long)reserve_max, (long long)validate_max, (long long)queue_max,
                 (long long)dispatch_max, (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                 (unsigned long)usb_rx_pause_count(), (unsigned long)bridge_dropped_records(),
                 buffer_capacity, (unsigned)buffer_retained,
                 (unsigned long)allocations, (unsigned long)failures);
        next_stats = now + 1000000;
#ifdef CONFIG_TOFFEE_PROFILE
        ESP_LOGI(
            TAG,
            "PC_STAGE_AVG:frames=%llu picture_bytes=%llu picture_max=%lu "
            "rx_excl_reserve_us=%llu reserve_us=%llu crc_us=%llu queue_us=%llu dispatch_us=%llu",
            (unsigned long long)frames, (unsigned long long)(bytes / frames),
            (unsigned long)largest_picture, (unsigned long long)(receive_total / frames),
            (unsigned long long)(reserve_total / frames), (unsigned long long)(crc_total / frames),
            (unsigned long long)(queue_total / frames),
            (unsigned long long)(dispatch_total / frames));
#endif
    }
}

static void usb_receiver_task(void* unused)
{
    (void)unused;
    record_parser_t parser = {.reserve = reserve_record, .finish = finish_record};
    uint8_t buffer[READ_BUFFER_SIZE];
    uint64_t received_bytes = 0;
    unsigned idle_reads = 0;
    while (true)
    {
        const int count = usb_serial_jtag_read_bytes(buffer, sizeof(buffer), pdMS_TO_TICKS(20));
        if (count > 0)
        {
            received_bytes += count;
            idle_reads = 0;
            record_feed(&parser, buffer, count);
        } else if (++idle_reads == ABANDON_IDLE_READS)
        {
            abandon_record(&parser);
        } else if (received_bytes && idle_reads == 50)
        {
            ESP_LOGI(TAG,
                     "PC_USB_RX_IDLE:bytes=%llu header=%u sequence=%lu length=%lu payload=%u "
                     "free_slots=%u ready_records=%u",
                     (unsigned long long)received_bytes, (unsigned)parser.header_used,
                     (unsigned long)parser.sequence, (unsigned long)parser.length,
                     (unsigned)parser.payload_used, (unsigned)uxQueueMessagesWaiting(free_slots),
                     (unsigned)uxQueueMessagesWaiting(ready_records));
        }
    }
}

void usb_stream_start(void)
{
    /* Assert the ROM adapter's seed/final-XOR convention against IEEE CRC32. */
    if (esp_rom_crc32_le(0, (const uint8_t*)"123456789", 9) != 0xcbf43926u)
        abort();
    free_slots = xQueueCreate(USB_RECORD_SLOTS, sizeof(record_slot_t*));
    ready_records = xQueueCreate(USB_RECORD_SLOTS, sizeof(usb_record_t));
    ring_space = xSemaphoreCreateBinary();
    buffer_mutex = xSemaphoreCreateMutex();
    if (!free_slots || !ready_records || !ring_space || !buffer_mutex)
        abort();
    for (unsigned i = 0; i < USB_RECORD_SLOTS; ++i)
    {
        record_slot_t* slot = &slots[i];
        if (xQueueSend(free_slots, &slot, 0) != pdTRUE)
            abort();
    }
    usb_serial_jtag_driver_config_t config{};
    config.tx_buffer_size = USB_TX_BUFFER_SIZE;
    config.rx_buffer_size = USB_RX_BUFFER_SIZE;
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
    usb_serial_jtag_vfs_use_driver();
    bridge_link_start();
    if (xTaskCreate(usb_receiver_task, "usb_video_rx", 6144, NULL, 6, NULL) != pdPASS)
        abort();
    ESP_LOGI(TAG,
             "USB pipeline ready: %u slots, %u-byte small buffers, large records sharing %u bytes",
             USB_RECORD_SLOTS, SMALL_PAYLOAD_SIZE, MAX_USB_PAYLOAD);
}
