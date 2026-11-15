#include "input_forwarder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "transport/bridge_link.h"
#include "common/bridge_record.h"
#include <cstring>
namespace {
struct Report {
    uint8_t bytes[BRIDGE_INPUT_REPORT_BYTES];
    uint32_t sequence;
};
QueueHandle_t latest = nullptr;
uint32_t sequence = 0;
void forward(void*)
{
    Report report;
    TickType_t wake = xTaskGetTickCount();
    while (true)
    {
        // Send only the newest report, so a slow USB reader cannot accumulate old input.
        if (xQueueReceive(latest, &report, 0) == pdTRUE)
            bridge_send(BRIDGE_INPUT, report.sequence, report.bytes, sizeof(report.bytes));
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(16));
    }
}
} // namespace
extern "C" void input_start()
{
    latest = xQueueCreate(1, sizeof(Report));
    if (!latest || xTaskCreate(forward, "input_usb", 3072, nullptr, 4, nullptr) != pdPASS)
        ESP_LOGE("input", "Could not start input forwarding");
}
extern "C" void input_receive(const uint8_t* bytes, size_t size)
{
    if (!latest || size != BRIDGE_INPUT_REPORT_BYTES)
        return;
    Report report;
    std::memcpy(report.bytes, bytes, size);
    report.sequence = sequence++;
    xQueueOverwrite(latest, &report);
}
