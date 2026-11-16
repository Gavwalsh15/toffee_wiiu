#pragma once
#include "capture_model.h"
#include "common/monotonic_clock.h"
#include "common/video_profile.h"
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>

namespace stream {
inline constexpr int VideoQp = 8;
inline constexpr const char* VideoPreset = "fast";

struct StatsWindow {
    quint64 frames = 0, bytes = 0;
    double maxAckMs = 0, maxEncodeMs = 0;
    qint64 startUs = 0, endUs = 0;
};

struct StreamStats {
    quint64 totalFrames = 0, bytes = 0, idrs = 0;
    double maxAckMs = 0, maxEncodeMs = 0;
    StatsWindow current, lastSecond;
    double maxSourceMs = 0, maxConvertMs = 0, maxEncoderPipeMs = 0;
    quint64 sourceJobs = 0, sourceUs = 0, convertUs = 0, encoderPipeUs = 0;
    quint64 freshCaptures = 0, reusedCaptures = 0;
    quint64 captureFirstSerial = 0, captureLastSerial = 0;
    quint64 encoderWaitTicks = 0, creditWaitTicks = 0, rawWaitTicks = 0;
};

class SessionLifecycle {
  public:
    enum class State {
        Disconnected,
        AwaitingPad,
        Ready,
        StartingEncoder,
        StartingBridge,
        Streaming,
        Stopping,
        Resetting,
        Faulted
    };
    State state() const { return state_; }
    std::uint64_t epoch() const { return epoch_; }
    bool active() const
    {
        return state_ == State::StartingEncoder || state_ == State::StartingBridge ||
               state_ == State::Streaming;
    }
    bool stopping() const { return state_ == State::Stopping; }
    bool resetting() const { return state_ == State::Resetting; }
    bool accepts(std::uint64_t epoch) const { return active() && epoch == epoch_; }
    void connected(bool ready)
    {
        if (!active() && !stopping() && !resetting())
            state_ = ready ? State::Ready : State::AwaitingPad;
    }
    void beginReset()
    {
        ++epoch_;
        state_ = State::Resetting;
    }
    bool start()
    {
        if (state_ != State::Ready)
            return false;
        ++epoch_;
        state_ = State::StartingEncoder;
        return true;
    }
    bool encoderStarted(std::uint64_t epoch)
    {
        if (!accepts(epoch) || state_ != State::StartingEncoder)
            return false;
        state_ = State::StartingBridge;
        return true;
    }
    bool bridgeStarted(std::uint64_t epoch)
    {
        if (!accepts(epoch) || state_ != State::StartingBridge)
            return false;
        state_ = State::Streaming;
        return true;
    }
    void stop()
    {
        if (!stopping())
        {
            ++epoch_;
            state_ = State::Stopping;
        }
    }
    void stopped(bool connected, bool ready)
    {
        state_ = connected ? (ready ? State::Ready : State::AwaitingPad) : State::Disconnected;
    }
    void fault()
    {
        ++epoch_;
        state_ = State::Faulted;
    }
    void disconnected()
    {
        ++epoch_;
        state_ = State::Disconnected;
    }

  private:
    State state_ = State::Disconnected;
    std::uint64_t epoch_ = 0;
};

class PictureSizeControl {
  public:
    static constexpr qsizetype FrameBytes = 11 * 1024; 
    static constexpr qsizetype SavedBytes = 8 * 1024;
    static constexpr int CalmPictures = 4;

    void begin(int sharpestQp)
    {
        sharpest_ = qp_ = sharpestQp;
        calm_ = 0;
        credit_ = SavedBytes;
    }
    int qp() const { return qp_; }
    void encoded(qsizetype bytes, bool idr)
    {
        if (bytes <= 0)
            return;
        credit_ = std::min(SavedBytes, credit_ + FrameBytes - bytes);
        // IDR pictures have their own quantizer, so their size says little about the next P.
        if (idr)
        {
            credit_ = std::max<qsizetype>(credit_, 0);
            return;
        }
        if (credit_ < 0)
        {
            // Size roughly halves for every 6 QP.
            raise(int(std::ceil(6 * std::log2(1.0 + double(-credit_) / FrameBytes))));
            credit_ = 0;
        } else if (credit_ == SavedBytes && ++calm_ >= CalmPictures)
        {
            calm_ = 0;
            qp_ = std::max(sharpest_, qp_ - 1);
        }
    }

  private:
    void raise(int steps)
    {
        calm_ = 0;
        qp_ = std::min(toffee::video::MAX_PICTURE_QP, qp_ + std::max(1, steps));
    }
    qsizetype credit_ = SavedBytes;
    int sharpest_ = 32, qp_ = 32, calm_ = 0;
};

// Pictures handed to the bridge and not yet reported done.
class DeliveryTracker {
  public:
    struct Sample {
        std::int64_t bytes = 0;
        bool idr = false;
        std::int64_t encoderUs = 0;
        std::int64_t queueUs = -1, writeUs = -1, writtenAt = -1;
    };
    struct Done {
        std::uint32_t sequence;
        std::int64_t sentAt;
        const Sample* sample; // null unless profiling
    };

    void clear()
    {
        sent_.clear();
        samples_.clear();
    }
    bool empty() const { return sent_.empty(); }
    std::int64_t oldestWaitUs(std::int64_t now) const
    {
        return sent_.empty() ? 0 : now - sent_.begin()->second;
    }
    void sent(std::uint32_t sequence, std::int64_t now) { sent_[sequence] = now; }
    void sent(std::uint32_t sequence, std::int64_t now, const Sample& sample)
    {
        sent(sequence, now);
        samples_[sequence] = sample;
    }
    void written(std::uint32_t sequence, std::int64_t queueUs, std::int64_t writeUs,
                 std::int64_t now)
    {
        const auto it = samples_.find(sequence);
        if (it == samples_.end())
            return;
        it->second.queueUs = queueUs;
        it->second.writeUs = writeUs;
        it->second.writtenAt = now;
    }
    template <class Handler> void completeThrough(std::int64_t completed, Handler&& handler)
    {
        while (!sent_.empty() && std::int64_t(sent_.begin()->first) <= completed)
        {
            const auto sequence = sent_.begin()->first;
            const auto sample = samples_.find(sequence);
            handler(Done{sequence, sent_.begin()->second,
                         sample == samples_.end() ? nullptr : &sample->second});
            if (sample != samples_.end())
                samples_.erase(sample);
            sent_.erase(sent_.begin());
        }
    }

  private:
    std::map<std::uint32_t, std::int64_t> sent_;
    std::map<std::uint32_t, Sample> samples_;
};

class FramePipeline {
  public:
    void reset(qint64 firstDeadline = 0)
    {
        preparing_ = false;
        prepareAt_ = nextPrepare_ = 0;
        nextFrame_ = firstDeadline;
        latest_ = {};
    }
    bool preparationDue(qint64 now) const { return !preparing_ && now >= nextPrepare_; }
    void preparing(qint64 now)
    {
        preparing_ = true;
        prepareAt_ = now;
        nextPrepare_ = pacing::advanceFrameDeadline(nextPrepare_, now);
    }
    bool preparationTimedOut(qint64 now) const { return preparing_ && now - prepareAt_ > 2000000; }
    bool hasFrame() const { return !latest_.pixels.isEmpty(); }
    void publish(PreparedFrame frame)
    {
        preparing_ = false;
        latest_ = std::move(frame);
    }
    const PreparedFrame& latest() const { return latest_; }
    bool encodeDue(qint64 now) const { return now >= nextFrame_; }
    qint64 lateness(qint64 now) const { return std::max<qint64>(0, now - nextFrame_); }
    PreparedFrame take(qint64 now)
    {
        nextFrame_ = pacing::advanceFrameDeadline(nextFrame_, now);
        return std::exchange(latest_, {});
    }

  private:
    PreparedFrame latest_;
    bool preparing_ = false;
    qint64 prepareAt_ = 0, nextPrepare_ = 0, nextFrame_ = 0;
};
} // namespace stream
