#include "stream_controller.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <QDateTime>
#include <algorithm>
StreamController::StreamController(QObject* parent) : QObject(parent), link_(clock_)
{
    clock_.start();
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(2);
    deliveryProfile_ = qEnvironmentVariableIsSet("TOFFEE_DELIVERY_PROFILE");
    link_.setProfiling(deliveryProfile_ || pacingTrace_.enabled());
    connect(&link_, &stream::BridgeLink::recordWritten, this,
            [this](qint64 tag, qint64 wait, qint64 write) {
                if (pacingTrace_.enabled())
                    pacingTrace_.add(QString("event=write at_us=%1 seq=%2 queue_us=%3 write_us=%4")
                                         .arg(pacing::nowUs())
                                         .arg(tag)
                                         .arg(wait)
                                         .arg(write)
                                         .toUtf8());
                delivery_.written(quint32(tag), wait, write, elapsedUs());
            });
    connect(&capture_, &stream::CaptureModel::changed, this, [this] {
        link_.feedback().idr = true;
        emit changed();
    });
    connect(&capture_, &stream::CaptureModel::previewChanged, this, &StreamController::previewChanged);
    connect(&capture_, &stream::CaptureModel::targetsChanged, this, &StreamController::captureChanged);
    connect(&capture_, &stream::CaptureModel::failed, this, [this](const QString& error) {
        note("Capture stopped: " + error);
        if (session_.active())
            stopStream();
    });
    connect(&timer_, &QTimer::timeout, this, &StreamController::tick);
    connect(&audio_, &AudioCapture::chunk, this, &StreamController::audioChunk,
            Qt::QueuedConnection);
    connect(
        &audio_, &AudioCapture::failed, this, [this](const QString& message) { note(message); },
        Qt::QueuedConnection);
    connect(&input_, &InputController::rumbleRequested, &rumble_,
            &stream::RumbleDriver::setStrength);
    connect(&rumble_, &stream::RumbleDriver::pulse, this, &StreamController::vibrate);
    connect(&rumble_, &stream::RumbleDriver::stopped, this, [this] { vibrate(0); });
    reconnectTimer_.setInterval(1000);
    connect(&reconnectTimer_, &QTimer::timeout, this, &StreamController::reconnectTick);
    connect(&link_, &stream::BridgeLink::receiving, this, [this] { statusBeforeReport_ = status_; });
    connect(&link_, &stream::BridgeLink::text, this, [this](const QByteArray& text) { log_.write(text); });
    connect(&link_, &stream::BridgeLink::inputReport, &input_, &InputController::report);
    connect(&link_, &stream::BridgeLink::incompatible, this, [this](const QString& message) {
        error_ = message;
        note(error_);
        disconnectDevice();
        emit bridgeIncompatible();
    });
    connect(&link_, &stream::BridgeLink::errorReported, this,
            [this](const QString& message) { fail("ESP: " + message); });
    connect(&link_, &stream::BridgeLink::lost, this, &StreamController::bridgeLost);
    connect(&link_, &stream::BridgeLink::updated, this, &StreamController::bridgeUpdated);
    connect(&link_, &stream::BridgeLink::failed, this, &StreamController::fail);
    connect(&link_, &stream::BridgeLink::resetFinished, this, [this] {
        if (!session_.resetting())
            return;
        session_.stopped(connected(), link_.feedback().ready);
        updateIdleStatus();
        emit changed();
    });
    connect(&encoder_, &stream::EncoderSession::recordReady, this,
            &StreamController::encoderOutput);
    connect(&encoder_, &stream::EncoderSession::diagnostic, this,
            [this](const QByteArray& bytes) { log_.write(bytes); });
    connect(&encoder_, &stream::EncoderSession::started, this, &StreamController::begin);
    connect(&encoder_, &stream::EncoderSession::stopped, this, &StreamController::finishStop);
    connect(&encoder_, &stream::EncoderSession::failed, this,
            [this](const QString& message, quint64 epoch) {
                if (!session_.accepts(epoch))
                    return;
                fail(message);
            });
    connect(qApp, &QCoreApplication::aboutToQuit, this, &StreamController::disconnectDevice);
    timer_.start();
}
StreamController::~StreamController()
{
    disconnectDevice();
    if (connected() && link_.busy())
        link_.drain(2000);
    closeTransport();
    log_.close();
}
QString StreamController::encoderPath() const
{
    if (!embeddedEncoderDirectory_)
        embeddedEncoderDirectory_ =
            std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/Toffee-XXXXXX"));
    if (!embeddedEncoderDirectory_->isValid())
        return {};
    const auto path = embeddedEncoderDirectory_->filePath(QStringLiteral("encoder.exe"));
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(101), RT_RCDATA);
    if (!resource)
        return {};
    HGLOBAL loaded = LoadResource(module, resource);
    const auto* data = static_cast<const char*>(LockResource(loaded));
    const DWORD size = SizeofResource(module, resource);
    if (!data || size == 0)
        return {};
    if (QFileInfo(path).size() == qint64(size))
        return path;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data, size) != qint64(size) ||
        !file.commit())
        return {};
    return path;
}
void StreamController::note(const QString& line)
{
    if (log_.isOpen())
    {
        log_.write(
            (QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) + " HOST " + line + "\n")
                .toUtf8());
    }
}
QString StreamController::metrics() const
{
    const auto& second = stats_.lastSecond;
    if (!session_.active() || !second.endUs)
        return summary();
    return metricsLine(second.frames, second.bytes, second.endUs - second.startUs,
                       second.maxAckMs, second.maxEncodeMs);
}
QString StreamController::summary() const
{
    const auto end = session_.active() ? elapsedUs() : stoppedAt_;
    return metricsLine(stats_.totalFrames, stats_.bytes, end - startedAt_, stats_.maxAckMs,
                       stats_.maxEncodeMs);
}
QString StreamController::metricsLine(quint64 frames, quint64 bytes, qint64 us, double ackMs,
                                      double encodeMs) const
{
    if (!stats_.totalFrames)
        return "No frames sent";
    const double seconds = std::max(0.001, us / 1000000.0);
    return QString("%1 fps · %2 Mbit/s · %3 frames · ACK max %4 ms · encode max %5 ms · %6 "
                   "recovery requests · QP %7")
        .arg(frames / seconds, 0, 'f', 1)
        .arg(bytes * 8 / seconds / 1000000, 0, 'f', 2)
        .arg(stats_.totalFrames)
        .arg(ackMs, 0, 'f', 1)
        .arg(encodeMs, 0, 'f', 1)
        .arg(link_.feedback().requests)
        .arg(pictureSize_.qp());
}

namespace stream {
namespace {
constexpr int FullStrength = 255;
constexpr int TickMs = 50;
// full strength stays on without gaps.
constexpr int PulseMs = 70;
} // namespace

RumbleDriver::RumbleDriver(QObject* parent) : QObject(parent)
{
    timer_.setInterval(TickMs);
    connect(&timer_, &QTimer::timeout, this, &RumbleDriver::tick);
}

void RumbleDriver::setStrength(int strength)
{
    strength_ = std::clamp(strength, 0, FullStrength);
    if (strength_ > 0)
    {
        if (!timer_.isActive())
        {
            credit_ = FullStrength; // start with a pulse straight away
            timer_.start();
        }
        return;
    }
    timer_.stop();
    emit stopped();
}

void RumbleDriver::tick()
{
    credit_ += strength_;
    if (credit_ >= FullStrength)
    {
        credit_ -= FullStrength;
        emit pulse(PulseMs);
    }
}
} // namespace stream
