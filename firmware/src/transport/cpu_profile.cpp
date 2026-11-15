#include "cpu_profile.h"
#include "sdkconfig.h"
#ifdef CONFIG_TOFFEE_PROFILE
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define PROFILE_TASKS 32
static TaskStatus_t tasks[PROFILE_TASKS];
static char names[PROFILE_TASKS][configMAX_TASK_NAME_LEN];
static struct {
    UBaseType_t id;
    configRUN_TIME_COUNTER_TYPE ticks;
} previous[PROFILE_TASKS];
static UBaseType_t previous_count;
static configRUN_TIME_COUNTER_TYPE previous_total;
static int64_t next_sample;

void cpu_profile_poll(void)
{
    const int64_t now = esp_timer_get_time();
    if (now < next_sample)
        return;
    next_sample = now + 5000000;
    configRUN_TIME_COUNTER_TYPE total = 0;
    vTaskSuspendAll();
    const UBaseType_t count = uxTaskGetSystemState(tasks, PROFILE_TASKS, &total);
    for (UBaseType_t i = 0; i < count; ++i)
    {
        strncpy(names[i], tasks[i].pcTaskName, sizeof(names[i]) - 1);
        names[i][sizeof(names[i]) - 1] = 0;
    }
    xTaskResumeAll();
    const int64_t snapshot_us = esp_timer_get_time() - now;
    if (!count)
    {
        ESP_LOGW("profile", "PC_CPU_PROFILE_ERROR:task_capacity=%u", PROFILE_TASKS);
        previous_count = 0;
        return;
    }
    const configRUN_TIME_COUNTER_TYPE elapsed = total - previous_total;
    if (previous_count && elapsed)
    {
        ESP_LOGI("profile", "PC_CPU_WINDOW:us=%llu snapshot_us=%lld tasks=%u",
                 (unsigned long long)elapsed, (long long)snapshot_us, (unsigned)count);
        for (UBaseType_t i = 0; i < count; ++i)
        {
            for (UBaseType_t j = 0; j < previous_count; ++j)
            {
                if (tasks[i].xTaskNumber != previous[j].id)
                    continue;
                const configRUN_TIME_COUNTER_TYPE used =
                    tasks[i].ulRunTimeCounter - previous[j].ticks;
                const unsigned permille = (unsigned)((uint64_t)used * 1000 / elapsed);
                ESP_LOGI("profile", "PC_CPU_TASK:name=%s permille=%u stack_free_min=%u", names[i],
                         permille, (unsigned)tasks[i].usStackHighWaterMark);
                break;
            }
        }
    }
    for (UBaseType_t i = 0; i < count; ++i)
    {
        previous[i].id = tasks[i].xTaskNumber;
        previous[i].ticks = tasks[i].ulRunTimeCounter;
    }
    previous_count = count;
    previous_total = total;
}
#else
void cpu_profile_poll(void) {}
#endif
