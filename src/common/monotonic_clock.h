#pragma once
#include "video_profile.h"
#include <QtGlobal>
#include <algorithm>
#include <chrono>

namespace pacing {
inline qint64 nowUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
// The next frame's deadline: keeps fractional timer periods, and drops a long stall's backlog.
inline std::int64_t advanceFrameDeadline(std::int64_t deadline, std::int64_t now)
{
    const auto period = toffee::video::FRAME_PERIOD_US;
    return deadline ? std::max(deadline + period, now) : now + period;
}
} // namespace pacing
