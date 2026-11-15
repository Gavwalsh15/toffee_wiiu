#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

typedef struct {
    int bytes, error;
    unsigned retries;
    int64_t waited_us;
} video_send_result_t;
typedef video_send_result_t (*video_submit_fn)(void*);
typedef int64_t (*video_clock_fn)(void*);
typedef void (*video_pause_fn)(void*);

static inline bool video_send_transient(int error)
{
    return error == ENOMEM || error == EAGAIN
#ifdef ENOBUFS
           || error == ENOBUFS
#endif
        ;
}

/* Retry only datagrams explicitly rejected by the socket. A frame shares one
 * budget across format and video packets; successful packets are never resent.
 * The pause may oversleep, so recheck the clock before another send attempt. */
static inline video_send_result_t video_send_with_retry(void* context, int expected,
                                                        int64_t* budget_us, video_submit_fn submit,
                                                        video_clock_fn clock, video_pause_fn pause)
{
    video_send_result_t result = submit(context);
    result.retries = 0;
    result.waited_us = 0;
    if (result.bytes != -1 || !video_send_transient(result.error) || *budget_us <= 0)
        return result;
    const int64_t started = clock(context), deadline = started + *budget_us;
    unsigned retries = 0;
    while (result.bytes == -1 && video_send_transient(result.error))
    {
        if (deadline - clock(context) < 1000)
            break;
        pause(context);
        if (clock(context) >= deadline)
            break;
        ++retries;
        result = submit(context);
        if (result.bytes == expected)
            break;
    }
    result.retries = retries;
    result.waited_us = clock(context) - started;
    *budget_us = result.waited_us >= *budget_us ? 0 : *budget_us - result.waited_us;
    return result;
}
