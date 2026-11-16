#include "stream_controller.h"
#include "common/video_profile.h"
#include <algorithm>

namespace {
constexpr qint64 FeedbackTimeoutUs = 10'000'000;
constexpr qint64 HelloTimeoutUs = 8'000'000;
constexpr qint64 BeginAckTimeoutUs = 3'000'000;
constexpr qint64 FrameAckTimeoutUs = 2'000'000;
constexpr qint64 IdleRefreshUs = 400'000;
constexpr qint64 StatsPeriodUs = 1'000'000;
} // namespace

void StreamController::bridgeLost()
{
    input_.bridgeLost();
    if (!session_.stopping() && !session_.resetting())
    {
        stopAction_ = StopAction::Stop;
        halt(false);
    }
    note("GamePad lost or ESP restarted; stopped. Start stream after readiness returns.");
}
void StreamController::bridgeUpdated(bool wasReady)
{
    const auto lastRx = link_.lastReceived();
    if (session_.active() && link_.feedback().completed >= qint64(sequence_))
    {
        fail("Unexpected frame completion sequence");
        return;
    }
    delivery_.completeThrough(link_.feedback().completed, [&](const auto& done) {
        const auto ackUs = lastRx - done.sentAt;
        if (pacingTrace_.enabled())
            pacingTrace_.add(QString("event=ack at_us=%1 seq=%2 total_us=%3")
                                 .arg(pacing::nowUs())
                                 .arg(done.sequence)
                                 .arg(ackUs)
                                 .toUtf8());
        stats_.maxAckMs = std::max(stats_.maxAckMs, ackUs / 1000.0);
        stats_.current.maxAckMs = std::max(stats_.current.maxAckMs, ackUs / 1000.0);
        if (const auto* sample = done.sample)
            note(QString("HOST_DELIVERY:seq=%1 bytes=%2 idr=%3 encoder_pipe_us=%4 queue_us=%5 "
                         "native_write_us=%6 ack_after_native_us=%7 ack_total_us=%8")
                     .arg(done.sequence)
                     .arg(sample->bytes)
                     .arg(sample->idr)
                     .arg(sample->encoderUs)
                     .arg(sample->queueUs)
                     .arg(sample->writeUs)
                     .arg(sample->writtenAt < 0 ? -1 : lastRx - sample->writtenAt)
                     .arg(ackUs));
    });
    if (link_.feedback().profile == stream::Profile && session_.bridgeStarted(session_.epoch()))
    {
        pipeline_.reset(lastRx);
        lastWrite_ = 0;
        status_ = "Streaming";
        note("BEGIN accepted · profile 5");
        startAudio();
    } else if (!session_.active() && !session_.stopping() && !session_.resetting())
    {
        session_.connected(ready());
        updateIdleStatus();
    }
    if (statusBeforeReport_ != status_ || wasReady != link_.feedback().ready ||
        pairingBeforeReport_ != link_.feedback().pairingCode)
        emit changed();
    pairingBeforeReport_ = link_.feedback().pairingCode;
}
void StreamController::encoderOutput(const QByteArray& record, bool idr, quint64 epoch)
{
    if (!session_.accepts(epoch))
        return;
    if (!ready() || link_.feedback().profile != stream::Profile)
    {
        link_.feedback().idr = true;
        return;
    }
    const auto now = elapsedUs();
    stats_.maxEncodeMs = std::max(stats_.maxEncodeMs, (now - encodeAt_) / 1000.0);
    stats_.current.maxEncodeMs = std::max(stats_.current.maxEncodeMs, (now - encodeAt_) / 1000.0);
    if (pacingTrace_.enabled())
        pacingTrace_.add(QString("event=encoded at_us=%1 seq=%2 pipe_us=%3 bytes=%4 idr=%5")
                             .arg(pacing::nowUs())
                             .arg(sequence_)
                             .arg(now - encoderWriteAt_)
                             .arg(record.size())
                             .arg(idr)
                             .toUtf8());
    stats_.maxEncoderPipeMs = std::max(stats_.maxEncoderPipeMs, (now - encoderWriteAt_) / 1000.0);
    stats_.encoderPipeUs += now - encoderWriteAt_;

    pictureSize_.encoded(idr && !submittedIdr_ ? toffee::video::MAX_RECORD_BYTES : record.size(),
                         idr && submittedIdr_);
    const auto bytes = stream::packet(VIDEO_FRAME, sequence_, record);
    if (deliveryProfile_)
        delivery_.sent(sequence_, now, {record.size(), idr, now - encoderWriteAt_});
    else
        delivery_.sent(sequence_, now);
    if (!link_.send(bytes, sequence_))
    {
        fail(link_.errorString());
        return;
    }
    ++sequence_;
    ++stats_.totalFrames;
    stats_.bytes += bytes.size();
    ++stats_.current.frames;
    stats_.current.bytes += bytes.size();
    stats_.idrs += idr;
    lastWrite_ = now;
}
void StreamController::tick()
{
    const auto now = elapsedUs();
    if (session_.active() && pacingTrace_.enabled() && lastTick_ &&
        now - lastTick_ > toffee::video::FRAME_PERIOD_US)
        pacingTrace_.add(QString("event=tick_gap at_us=%1 gap_us=%2")
                             .arg(pacing::nowUs())
                             .arg(now - lastTick_)
                             .toUtf8());
    if (!capture_.paused() && !capture_.scene().capturing())
        capture_.scene().advance((now - lastTick_) / 1000000.0, input_.active(), input_.axes(),
                                 input_.buttons());
    lastTick_ = now;
    capture_.tick(now, session_.active() || session_.stopping());
    if (connected() && !session_.stopping() && !session_.resetting() &&
        link_.waitingForHello() > HelloTimeoutUs)
    {
        error_ = "The bridge did not answer. Flash the current firmware from the Flash ESP page, "
                 "or pick another port.";
        note(error_);
        disconnectDevice();
        return;
    }
    if (connected() && ready() && link_.quietFor() > FeedbackTimeoutUs)
    {
        fail("ESP feedback timed out");
        return;
    }
    if (session_.stopping())
    {
        finishStop();
        return;
    }
    if (!session_.active())
        return;
    if (session_.state() == stream::SessionLifecycle::State::StartingBridge)
    {
        if (now - beginAt_ > BeginAckTimeoutUs)
            fail("ESP did not acknowledge stream start within 3 seconds");
        return;
    }
    if (session_.state() == stream::SessionLifecycle::State::StartingEncoder)
        return;
    if (delivery_.oldestWaitUs(now) > FrameAckTimeoutUs)
    {
        fail("ESP frame completion timed out");
        return;
    }
    if (pipeline_.preparationTimedOut(now))
    {
        fail("Frame preparation timed out");
        return;
    }
    if (!ready() || link_.feedback().profile != stream::Profile)
        return;
    if (pipeline_.preparationDue(now))
        prepareFrame(now);
    if (encoder_.busy())
    {
        if (pipeline_.encodeDue(now))
            ++stats_.encoderWaitTicks;
        return;
    }
    if (!pipeline_.encodeDue(now))
        return;
    if (!pipeline_.hasFrame())
    {
        ++stats_.rawWaitTicks;
        return;
    }
    if (!link_.feedback().canSend(sequence_))
    {
        ++stats_.creditWaitTicks;
        return;
    }
    const bool idr = link_.feedback().chooseIdr(sequence_, now - lastWrite_ > IdleRefreshUs);
    if (pacingTrace_.enabled())
        pacingTrace_.add(
            QString("event=submit at_us=%1 seq=%2 serial=%3 published_us=%4 prepare_id=%5 "
                    "late_us=%6 encoder_wait_ticks=%7 credit_wait_ticks=%8 raw_wait_ticks=%9")
                .arg(pacing::nowUs())
                .arg(sequence_)
                .arg(pipeline_.latest().serial)
                .arg(pipeline_.latest().publishedUs)
                .arg(pipeline_.latest().prepareAt)
                .arg(pipeline_.lateness(now))
                .arg(stats_.encoderWaitTicks)
                .arg(stats_.creditWaitTicks)
                .arg(stats_.rawWaitTicks)
                .toUtf8());
    stats_.encoderWaitTicks = stats_.creditWaitTicks = stats_.rawWaitTicks = 0;
    auto frame = pipeline_.take(now);
    encodeAt_ = frame.prepareAt;
    encoderWriteAt_ = now;
    submittedIdr_ = idr;
    if (!encoder_.submit(frame.pixels, idr, pictureSize_.qp()))
    {
        if (session_.active())
            fail("Encoder input write failed");
        return;
    }
    if (now - lastNotify_ > StatsPeriodUs)
    {
        lastNotify_ = now;
        stats_.current.endUs = now;
        stats_.lastSecond = std::exchange(stats_.current, {});
        stats_.current.startUs = now;
        note(metrics());
        note(QString("HOST_STAGE_STATS:source_max_ms=%1 convert_max_ms=%2 encoder_pipe_max_ms=%3 "
                     "source_avg_ms=%4 convert_avg_ms=%5 encoder_pipe_avg_ms=%6")
                 .arg(stats_.maxSourceMs, 0, 'f', 2)
                 .arg(stats_.maxConvertMs, 0, 'f', 2)
                 .arg(stats_.maxEncoderPipeMs, 0, 'f', 2)
                 .arg(stats_.sourceJobs ? stats_.sourceUs / (1000.0 * stats_.sourceJobs) : 0, 0,
                      'f', 2)
                 .arg(stats_.sourceJobs ? stats_.convertUs / (1000.0 * stats_.sourceJobs) : 0, 0,
                      'f', 2)
                 .arg(stats_.totalFrames ? stats_.encoderPipeUs / (1000.0 * stats_.totalFrames) : 0,
                      0, 'f', 2));
        if (capture_.scene().capturing())
            note(QString("HOST_CAPTURE_STATS:fresh=%1 reused=%2 backend_frames=%3 width=%4 height=%5")
                     .arg(stats_.freshCaptures)
                     .arg(stats_.reusedCaptures)
                     .arg(stats_.captureFirstSerial
                              ? stats_.captureLastSerial - stats_.captureFirstSerial + 1
                              : 0)
                     .arg(captureSize_.width())
                     .arg(captureSize_.height()));
        emit changed();
    }
}
void StreamController::prepareFrame(qint64 now)
{
    pipeline_.preparing(now);
    const auto epoch = session_.epoch();
    capture_.preparer().request(capture_.request(now), [this, epoch](stream::PreparedFrame frame) {
        if (!session_.accepts(epoch))
            return;
        if (frame.pixels.isEmpty())
        {
            capture_.fail("Capture stopped producing a usable image.");
            stopStream();
            return;
        }
        if (frame.capture)
            capture_.keep(frame.image, frame.pixels);
        if (frame.capture && !frame.frozen)
        {
            if (frame.reused)
                ++stats_.reusedCaptures;
            else
                ++stats_.freshCaptures;
            captureSize_ = frame.sourceSize;
            if (!stats_.captureFirstSerial)
                stats_.captureFirstSerial = frame.serial;
            stats_.captureLastSerial = frame.serial;
        }
        stats_.maxSourceMs = std::max(stats_.maxSourceMs, frame.sourceUs / 1000.0);
        stats_.maxConvertMs = std::max(stats_.maxConvertMs, frame.convertUs / 1000.0);
        ++stats_.sourceJobs;
        stats_.sourceUs += frame.sourceUs;
        stats_.convertUs += frame.convertUs;
        if (pacingTrace_.enabled())
            pacingTrace_.add(QString("event=prepare at_us=%1 prepare_id=%2 worker_start_us=%3 "
                                     "worker_done_us=%4 source_us=%5 convert_us=%6 serial=%7 "
                                     "published_us=%8 reused=%9 replaced=%10 requested_us=%11")
                                 .arg(pacing::nowUs())
                                 .arg(frame.prepareAt)
                                 .arg(frame.workerStart)
                                 .arg(frame.workerDone)
                                 .arg(frame.sourceUs)
                                 .arg(frame.convertUs)
                                 .arg(frame.serial)
                                 .arg(frame.publishedUs)
                                 .arg(frame.reused)
                                 .arg(pipeline_.hasFrame())
                                 .arg(frame.requestedUs)
                                 .toUtf8());
        pipeline_.publish(std::move(frame));
    });
}
