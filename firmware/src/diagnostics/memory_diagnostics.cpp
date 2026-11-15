#include "memory_diagnostics.h"
#ifdef CONFIG_TOFFEE_MEMORY_DIAGNOSTICS
#include "event_mailbox.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <atomic>

namespace {
constexpr unsigned Capacity = 8;
constexpr uint32_t DmaCaps = MALLOC_CAP_8BIT | MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL;
constexpr const char* Tag = "memory_diag";
struct Event {
    int64_t at;
    size_t size;
    const char* allocator; // SDK passes static __func__ strings.
    uint32_t caps, before, after;
    int error;
};
diagnostics::EventMailbox<Event, Capacity> events;
std::atomic<unsigned> allocationFailures{0}, socketRejections{0};
int64_t nextSample;

void allocationFailed(size_t size, uint32_t caps, const char* allocator)
{
    const unsigned count = allocationFailures.fetch_add(1, std::memory_order_relaxed) + 1;
    events.tryPush({esp_timer_get_time(), size, allocator, caps, count - 1, count, 0});
}
} // namespace

void memory_diagnostics_start(void)
{
    ESP_ERROR_CHECK(heap_caps_register_failed_alloc_callback(allocationFailed));
    ESP_LOGI(Tag, "MEM_DIAG_READY:slots=%u dynamic_tx=%d rx_static=%d rx_dynamic=%d", Capacity,
             CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM, CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM,
             CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM);
}

uint32_t memory_diagnostics_failure_count(void)
{
    return allocationFailures.load(std::memory_order_relaxed);
}

void memory_diagnostics_rejected(unsigned bytes, int error, uint32_t before)
{
    socketRejections.fetch_add(1, std::memory_order_relaxed);
    events.tryPush({esp_timer_get_time(), bytes, nullptr, 0, before,
                    memory_diagnostics_failure_count(), error});
}

void memory_diagnostics_poll(void)
{
    Event batch[Capacity];
    unsigned count = 0;
    while (count < Capacity && events.tryPop(batch[count]))
        ++count;
    for (unsigned i = 0; i < count; ++i)
    {
        const auto& event = batch[i];
        if (event.allocator)
        {
            ESP_LOGW(Tag, "MEM_ALLOC_FAILURE:at_us=%lld count=%lu bytes=%u caps=0x%lx allocator=%s",
                     (long long)event.at, (unsigned long)event.after, (unsigned)event.size,
                     (unsigned long)event.caps, event.allocator);
        } else
        {
            ESP_LOGW(
                Tag,
                "MEM_SOCKET_REJECT:at_us=%lld bytes=%u errno=%d alloc_before=%lu alloc_after=%lu",
                (long long)event.at, (unsigned)event.size, event.error, (unsigned long)event.before,
                (unsigned long)event.after);
        }
    }
    const int64_t now = esp_timer_get_time();
    if (now < nextSample)
        return;
    nextSample = now + 1000000;
    ESP_LOGI(Tag,
             "MEM_SAMPLE:at_us=%lld dma_free=%u dma_largest=%u dma_min=%u "
             "free=%u largest=%u rtc_free=%u alloc_failures=%lu socket_rejections=%lu dropped=%u",
             (long long)now, (unsigned)heap_caps_get_free_size(DmaCaps),
             (unsigned)heap_caps_get_largest_free_block(DmaCaps),
             (unsigned)heap_caps_get_minimum_free_size(DmaCaps),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_RTCRAM),
             (unsigned long)memory_diagnostics_failure_count(),
             (unsigned long)socketRejections.load(std::memory_order_relaxed), events.dropped());
}
#endif
