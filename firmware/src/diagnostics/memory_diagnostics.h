#pragma once
#include <stdint.h>
#include <sdkconfig.h>
#include "esp_heap_caps.h"

#define HEAP_STATS_FMT "free=%u largest=%u dma_free=%u dma_largest=%u rtc_free=%u"
#define HEAP_STATS_ARGS                                                                        \
    (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),                                        \
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),                           \
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL), \
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_DMA |          \
                                                   MALLOC_CAP_INTERNAL),                       \
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_RTCRAM)

#ifdef __cplusplus
extern "C" {
#endif
#ifdef CONFIG_TOFFEE_MEMORY_DIAGNOSTICS
void memory_diagnostics_start(void);
void memory_diagnostics_poll(void);
uint32_t memory_diagnostics_failure_count(void);
void memory_diagnostics_rejected(unsigned bytes, int error, uint32_t before);
#else
static inline void memory_diagnostics_start(void) {}
static inline void memory_diagnostics_poll(void) {}
static inline uint32_t memory_diagnostics_failure_count(void) { return 0; }
static inline void memory_diagnostics_rejected(unsigned bytes, int error, uint32_t before)
{
    (void)bytes;
    (void)error;
    (void)before;
}
#endif
#ifdef __cplusplus
}
#endif
