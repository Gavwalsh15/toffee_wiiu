#include "stream_controller.h"
#include <QDateTime>
#include <QDir>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QScopedValueRollback>
#include <QSerialPortInfo>
#include <algorithm>

namespace {
constexpr qint64 StopDrainTimeoutUs = 2'000'000;
constexpr int ReconnectAttempts = 60; // one a second
} // namespace

void StreamController::connectDevice(const QString& port)
{
    if (connected() || session_.stopping() || !encoder_.idle() || port.isEmpty())
        return;
    error_.clear();
    link_.forget();
    sequence_ = 0;
    stats_.totalFrames = stats_.bytes = stats_.idrs = 0;
    input_.reset();
    if (!link_.open(port))
    {
        fail(link_.errorString());
        return;
    }
    auto path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/logs";
    QDir().mkpath(path);
    log_.setFileName(path + "/session-" +
                     QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") + ".log");
    if (!log_.open())
    {
        error_ = "Cannot create session log: " + log_.errorString();
        link_.close();
        emit changed();
        return;
    }
    lastPort_ = port;
    session_.connected(false);
    status_ = "Port open · waiting for GamePad";
    note(status_ + " on " + port);
    emit changed();
}
void StreamController::begin(quint64 epoch)
{
    if (!ready() || !encoder_.running() || !session_.encoderStarted(epoch))
        return;
    link_.feedback().beginSession();
    sequence_ = 0;
    delivery_.clear();
    pipeline_.reset();
    beginAt_ = elapsedUs();
    status_ = "Starting stream";
    if (!link_.send(stream::packet(STREAM_BEGIN, stream::Profile, stream::beginPayload())))
        fail(link_.errorString());
    emit changed();
}
void StreamController::startStream()
{
    if (session_.active() || !ready())
        return;
    if (!sourceReady())
    {
        error_ = "Wait for the capture preview before starting.";
        emit changed();
        return;
    }
    const auto encoder = encoderPath();
    if (encoder.isEmpty())
    {
        error_ = "Could not unpack the encoder to a temporary folder.";
        emit changed();
        return;
    }
    if (!encoder_.idle() || !session_.start())
        return;
    capture_.cancel();
    error_.clear();
    stats_ = {};
    if (pacingTrace_.enabled())
        pacingTrace_.add(QString("event=start at_us=%1 mode=%2 frozen=%3 period_us=%4")
                             .arg(pacing::nowUs())
                             .arg(capture_.mode())
                             .arg(capture_.paused())
                             .arg(toffee::video::FRAME_PERIOD_US)
                             .toUtf8());
    link_.feedback().requests = 0;
    startedAt_ = elapsedUs();
    status_ = "Starting encoder";
    note(status_);
    pictureSize_.begin(stream::VideoQp);
    note(QString("Video: QP %1, %2 preset").arg(stream::VideoQp).arg(stream::VideoPreset));
    if (!encoder_.start(encoder, {}, session_.epoch(), stream::VideoQp, stream::VideoPreset))
    {
        fail("Encoder launch rejected");
        return;
    }
    emit changed();
}
void StreamController::startAudio()
{
    if (!audioEnabled_)
        return;
    audio_.setGain(audioGain_);
    adpcm_[0] = adpcm_[1] = {};
    audio_.start();
}
void StreamController::setAudioGain(double value)
{
    value = std::clamp(value, 0.5, 4.0);
    if (value == audioGain_)
        return;
    audioGain_ = value;
    audio_.setGain(value);
    emit changed();
}
void StreamController::vibrate(int milliseconds)
{
    if (!linkLive())
        return;
    QByteArray payload;
    stream::append32(payload, quint32(std::clamp(milliseconds, 0, 5000)));
    link_.send(stream::packet(VIBRATE, audioSequence_++, payload));
}
void StreamController::setAudioEnabled(bool value)
{
    if (value == audioEnabled_)
        return;
    audioEnabled_ = value;
    if (!value)
        audio_.stop();
    else if (session_.state() == stream::SessionLifecycle::State::Streaming)
        startAudio();
    emit changed();
}
void StreamController::audioChunk(const QByteArray& pcm)
{
    // Queued chunks can outlive the stream that produced them.
    if (!audioEnabled_ || !linkLive())
        return;
    QByteArray payload(ADPCM_BLOCK_BYTES, Qt::Uninitialized);
    adpcm_encode_block(adpcm_, reinterpret_cast<const uint8_t*>(pcm.constData()),
                       reinterpret_cast<uint8_t*>(payload.data()));
    // Audio goes ahead of queued video, so it waits for one picture at most rather than three.
    link_.send(stream::packet(AUDIO_DATA_ADPCM, audioSequence_++, payload), -1, true);
}
void StreamController::halt(bool sendEnd)
{
    if (halting_)
        return;
    QScopedValueRollback<bool> haltGuard(halting_, true);
    audio_.stop();
    const bool wasWanted = session_.active();
    capture_.cancel();
    if (wasWanted)
        stoppedAt_ = elapsedUs();
    if (wasWanted && pacingTrace_.enabled())
        pacingTrace_.add(QString("event=stop at_us=%1 frames=%2")
                             .arg(pacing::nowUs())
                             .arg(stats_.totalFrames)
                             .toUtf8());
    session_.stop();
    pipeline_.reset();
    encoder_.stop();
    stopDeadline_ = elapsedUs() + StopDrainTimeoutUs;
    if (sendEnd && wasWanted && connected())
    {
        if (!link_.send(stream::packet(STREAM_END, sequence_)))
        {
            if (error_.isEmpty())
                error_ = link_.errorString();
            stopAction_ = StopAction::Fault;
            closeTransport();
        }
    } else if (!sendEnd)
    {
        if (connected() && !link_.resetOutput())
        {
            error_ = link_.errorString();
            stopAction_ = StopAction::Fault;
            closeTransport();
        }
    }
    delivery_.clear();
    link_.feedback().profile = -1;
    if (wasWanted)
        note("Stopped: " + metrics() + QString(" · IDRs %1").arg(stats_.idrs));
    status_ = "Stopping stream";
    emit changed();
    finishStop();
}
void StreamController::closeTransport()
{
    link_.close();
    input_.clear();
}
void StreamController::updateIdleStatus()
{
    status_ = connected() ? (link_.feedback().ready ? "GamePad ready" : "Port open · waiting for GamePad")
                          : "Disconnected";
}
void StreamController::finishStop()
{
    if (!session_.stopping())
        return;
    if (link_.busy() && elapsedUs() >= stopDeadline_)
    {
        if (error_.isEmpty())
            error_ = "USB stop did not drain; restart ESP before streaming again.";
        note(error_);
        stopAction_ = StopAction::Fault;
        closeTransport();
    }
    if (!encoder_.idle() || link_.busy())
        return;
    const auto action = stopAction_;
    if (action == StopAction::Disconnect || action == StopAction::Fault)
    {
        if (log_.isOpen())
        {
            if (!log_.closing())
            {
                if (action == StopAction::Disconnect)
                    note("Disconnected");
                log_.requestClose();
            }
            if (!log_.finished())
                return;
        }
        stopAction_ = StopAction::Stop;
        closeTransport();
        link_.forget();
        if (action == StopAction::Fault)
        {
            session_.fault();
            status_ = "Connection stopped";
            armReconnect();
        } else
        {
            session_.disconnected();
            status_ = "Disconnected";
        }
        log_.close();
        emit changed();
        emit deviceDisconnected();
        return;
    }
    stopAction_ = StopAction::Stop;
    session_.stopped(connected(), link_.feedback().ready);
    updateIdleStatus();
    if (action == StopAction::Restart && connected())
    {
        link_.forget();
        session_.beginReset();
        link_.clearInput();
        status_ = "Restarting ESP";
        note(status_);
        if (!link_.resetPulse())
        {
            fail("Could not reset ESP: " + link_.errorString());
            return;
        }
    }
    emit changed();
}
void StreamController::stopStream()
{
    cancelReconnect();
    if (!session_.active())
        return;
    stopAction_ = StopAction::Stop;
    halt(true);
}
void StreamController::restartDevice()
{
    if (!connected() || session_.stopping() || session_.resetting())
        return;
    stopAction_ = StopAction::Restart;
    halt(true);
}
void StreamController::forgetPairing()
{
    if (!connected() || session_.stopping() || session_.resetting())
        return;
    QByteArray payload;
    stream::append32(payload, PAIR_RESET_MAGIC);
    const auto code = quint32(QRandomGenerator::global()->bounded(256));
    stream::append32(payload, code);
    note("Asking the bridge to forget its GamePad and restart into pairing with " +
         stream::pairingNames(int(code)));
    // The bridge restarts itself, so the port drops and the usual reconnect handling follows.
    link_.send(stream::packet(PAIR_RESET, audioSequence_++, payload), -1, true);
}
void StreamController::disconnectDevice()
{
    cancelReconnect();
    if (session_.stopping())
    {
        stopAction_ = StopAction::Disconnect;
        finishStop();
        return;
    }
    stopAction_ = StopAction::Disconnect;
    halt(true);
}
void StreamController::fail(const QString& message)
{
    if (link_.closing())
        return;
    if (session_.active())
        resume_ = true;
    error_ = message;
    note(message);
    stopAction_ = StopAction::Fault;
    closeTransport();
    halt(false);
}
void StreamController::armReconnect()
{
    if (lastPort_.isEmpty() || link_.closing() || reconnectTimer_.isActive())
        return;
    reconnectTries_ = 0;
    reconnectTimer_.start();
    note("Connection lost; trying to reconnect to " + lastPort_);
}
void StreamController::cancelReconnect()
{
    reconnectTimer_.stop();
    resume_ = false;
    reconnectTries_ = 0;
}
void StreamController::reconnectTick()
{
    if (session_.stopping() || session_.resetting() || !encoder_.idle())
        return;
    if (++reconnectTries_ > ReconnectAttempts)
    {
        note("Reconnect gave up");
        status_ = "Disconnected";
        cancelReconnect();
        emit changed();
        return;
    }
    if (!connected())
    {
        const auto ports = QSerialPortInfo::availablePorts();
        const bool present = std::any_of(ports.begin(), ports.end(), [this](const auto& info) {
            return info.portName() == lastPort_;
        });
        status_ = "Reconnecting to " + lastPort_;
        emit changed();
        if (present)
            connectDevice(lastPort_);
        return;
    }
    if (!resume_)
    {
        reconnectTimer_.stop();
        return;
    }
    if (ready() && !session_.active())
    {
        cancelReconnect();
        note("Reconnected; resuming stream");
        startStream();
    }
}
