#pragma once
#include "common/video_profile.h"
#include <stdbool.h>
#include <stdint.h>

#define VIDEO_PC_IDLE_TIMEOUT_MS 500
#define VIDEO_IDR_NOTICE_RETRY_MS 250
typedef struct {
    int64_t next_deadline;
} video_schedule_t;
static inline int64_t video_schedule_slot(video_schedule_t* schedule, int64_t now)
{
    if (!schedule->next_deadline || now - schedule->next_deadline > VIDEO_FRAME_PERIOD_US)
        schedule->next_deadline = now;
    return schedule->next_deadline;
}
static inline void video_schedule_advance(video_schedule_t* schedule)
{
    schedule->next_deadline += VIDEO_FRAME_PERIOD_US;
}
static inline bool video_pc_active(uint32_t now_ms, uint32_t last_pc_ms)
{
    return last_pc_ms != 0 && (uint32_t)(now_ms - last_pc_ms) < VIDEO_PC_IDLE_TIMEOUT_MS;
}
static inline uint32_t video_pc_timestamp(int64_t tsf, int64_t before, int64_t after, int64_t slot)
{
    const int64_t sample = before + (after - before) / 2;
    return (uint32_t)(tsf - (sample - slot));
}
typedef enum {
    VIDEO_ACTION_WAIT,
    VIDEO_ACTION_STANDBY,
    VIDEO_ACTION_FORMAT_ONLY
} video_idle_action_t;
static inline video_idle_action_t video_idle_action(bool ready, bool pc_active,
                                                    int64_t next_deadline, int64_t now)
{
    if (ready && !pc_active)
        return VIDEO_ACTION_STANDBY;
    if (ready && pc_active && next_deadline && now >= next_deadline)
        return VIDEO_ACTION_FORMAT_ONLY;
    return VIDEO_ACTION_WAIT;
}

// -1 leaves an atomic flag unchanged, so each flag is stored on its own rather than
// read-modified-written alongside the others.
typedef struct {
    int8_t started, need_idr, recovering;
} video_recovery_effect_t;
typedef enum {
    VIDEO_BEGIN_IDR,
    VIDEO_SUBMITTED_IDR,
    VIDEO_SUBMITTED_P,
    VIDEO_LOCAL_FAILURE,
    VIDEO_READY_LOST,
    VIDEO_READY_GAINED
} video_recovery_event_t;
static inline video_recovery_effect_t video_recovery_effect(video_recovery_event_t event, bool pc)
{
    switch (event)
    {
    case VIDEO_BEGIN_IDR:
        return (video_recovery_effect_t){-1, 0, 1};
    case VIDEO_SUBMITTED_IDR:
        return (video_recovery_effect_t){1, (int8_t)(pc ? 0 : -1), -1};
    case VIDEO_SUBMITTED_P:
        return (video_recovery_effect_t){-1, -1, 0};
    case VIDEO_LOCAL_FAILURE:
        return (video_recovery_effect_t){0, 1, 0};
    case VIDEO_READY_LOST:
        return (video_recovery_effect_t){0, -1, -1};
    case VIDEO_READY_GAINED:
        return (video_recovery_effect_t){-1, 1, -1};
    }
    return (video_recovery_effect_t){-1, -1, -1};
}
static inline bool video_recovery_accept_request(bool pc_active, bool recovering)
{
    return pc_active || !recovering;
}
// The pad keeps asking until it can decode, so a pending request is re-sent every
// VIDEO_IDR_NOTICE_RETRY_MS in case the earlier notice was lost.
static inline bool video_recovery_notify(bool already_pending, uint32_t now_ms,
                                         uint32_t last_notice_ms)
{
    return !already_pending || !last_notice_ms ||
           (uint32_t)(now_ms - last_notice_ms) >= VIDEO_IDR_NOTICE_RETRY_MS;
}
static inline bool video_recovery_can_submit(bool idr, bool reference_started)
{
    return idr || reference_started;
}
static inline bool video_hold_allowed(bool reference_started, bool need_idr)
{
    return reference_started && !need_idr;
}
