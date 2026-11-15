#pragma once
#include "audio/audio_capture.h"
#include "capture_model.h"
#include "common/ima_adpcm.h"
#include "common/pacing_trace.h"
#include "bridge_link.h"
#include "encoder_session.h"
#include "inputs/input_controller.h"
#include "logger/session_log.h"
#include "stream_state.h"
#include "stream/protocol.h"
#include "video/scene.h"
#include <QElapsedTimer>
#include <QMap>
#include <QObject>
#include <QTimer>
#include <QTemporaryDir>
#include <memory>

namespace stream {
// Turns a game's rumble strength (0-255) into GamePad pulses. The GamePad has one motor that is
// either on or off, so the strength becomes how often it is switched on.
class RumbleDriver : public QObject {
    Q_OBJECT
  public:
    explicit RumbleDriver(QObject* parent = nullptr);
    void setStrength(int strength);
  signals:
    void pulse(int milliseconds);
    void stopped();

  private:
    void tick();
    QTimer timer_;
    int strength_ = 0;
    int credit_ = 0;
};
} // namespace stream

class StreamController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(QString pairingCode READ pairingCode NOTIFY changed)
    Q_PROPERTY(bool streaming READ streaming NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString metrics READ metrics NOTIFY changed)
    Q_PROPERTY(QString portName READ portName NOTIFY changed)
    Q_PROPERTY(QString logPath READ logPath NOTIFY changed)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY changed)
    Q_PROPERTY(bool audioEnabled READ audioEnabled WRITE setAudioEnabled NOTIFY changed)
    Q_PROPERTY(double audioGain READ audioGain WRITE setAudioGain NOTIFY changed)
    Q_PROPERTY(InputController* input READ input CONSTANT)
    Q_PROPERTY(int sceneMode READ sceneMode WRITE setSceneMode NOTIFY previewChanged)
    Q_PROPERTY(QVariantList captureTargets READ captureTargets NOTIFY captureChanged)
    Q_PROPERTY(int captureTarget READ captureTarget WRITE setCaptureTarget NOTIFY captureChanged)
    Q_PROPERTY(bool sourceReady READ sourceReady NOTIFY previewChanged)
    Q_PROPERTY(QString captureError READ captureError NOTIFY captureChanged)
  public:
    enum SceneMode {
        Interactive = video::Scene::Interactive,
        DisplayCapture = video::Scene::DisplayCapture,
        WindowCapture = video::Scene::WindowCapture
    };
    Q_ENUM(SceneMode)

    explicit StreamController(QObject* parent = nullptr);
    ~StreamController() override;
    bool connected() const { return link_.isOpen(); }
    bool ready() const
    {
        return connected() && link_.feedback().ready && !session_.stopping() && !session_.resetting();
    }
    bool streaming() const { return session_.active(); }
    bool busy() const { return session_.active() || session_.stopping() || session_.resetting(); }
    QString status() const { return status_; }
    QString pairingCode() const { return stream::pairingSymbols(link_.feedback().pairingCode); }
    int pairingCodeValue() const { return link_.feedback().pairingCode; }
    QString error() const { return error_; }
    QString metrics() const;
    QString portName() const { return link_.portName(); }
    QString logPath() const { return log_.fileName(); }
    bool paused() const { return capture_.paused(); }
    InputController* input() { return &input_; }
    int sceneMode() const { return capture_.mode(); }
    void setSceneMode(int value)
    {
        if (sourceEditable())
            capture_.setMode(value);
    }
    QVariantList captureTargets() const { return capture_.targets(); }
    int captureTarget() const { return capture_.target(); }
    void setCaptureTarget(int index)
    {
        if (sourceEditable())
            capture_.select(index);
    }
    bool sourceReady() const { return capture_.sourceReady(); }
    QString captureError() const { return capture_.error(); }
    QImage previewImage() const { return capture_.preview(); }
    Q_INVOKABLE void refreshCaptureTargets()
    {
        if (sourceEditable())
            capture_.refresh();
    }
    // name is a Windows display name such as \\.\DISPLAY5.
    Q_INVOKABLE void captureDisplay(const QString& name)
    {
        if (sourceEditable())
            capture_.selectDisplay(name);
    }
    void setPaused(bool value) { capture_.setPaused(value); }
    bool audioEnabled() const { return audioEnabled_; }
    void setAudioEnabled(bool value);
    double audioGain() const { return audioGain_; }
    void setAudioGain(double value);
    Q_INVOKABLE void connectDevice(const QString& port);
    Q_INVOKABLE void disconnectDevice();
    Q_INVOKABLE void startStream();
    Q_INVOKABLE void stopStream();
    Q_INVOKABLE void restartDevice();
    Q_INVOKABLE void forgetPairing();
    Q_INVOKABLE void vibrate(int milliseconds);
  signals:
    void changed();
    void previewChanged();
    void captureChanged();
    void deviceDisconnected();

  private:
    void tick();
    void bridgeUpdated(bool wasReady);
    void bridgeLost();
    void encoderOutput(const QByteArray& record, bool idr, quint64 epoch);
    void finishStop();
    void closeTransport();
    void updateIdleStatus();
    void prepareFrame(qint64 now);
    void fail(const QString& message);
    void startAudio();
    void audioChunk(const QByteArray& pcm);
    void armReconnect();
    void cancelReconnect();
    void reconnectTick();
    void halt(bool sendEnd);
    void begin(quint64 epoch);
    QString encoderPath() const;
    void note(const QString& line);
    bool sourceEditable() const { return !session_.active() && !session_.stopping(); }
    qint64 elapsedUs() const { return clock_.nsecsElapsed() / 1000; }
    bool linkLive() const
    {
        return !halting_ && session_.state() == stream::SessionLifecycle::State::Streaming &&
               ready();
    }

    QElapsedTimer clock_;
    stream::BridgeLink link_;
    InputController input_;
    mutable std::unique_ptr<QTemporaryDir> embeddedEncoderDirectory_;
    stream::EncoderSession encoder_;
    stream::SessionLifecycle session_;
    enum class StopAction { Stop, Disconnect, Restart, Fault };
    StopAction stopAction_ = StopAction::Stop;
    qint64 stopDeadline_ = 0;
    QString statusBeforeReport_;
    int pairingBeforeReport_ = -1;
    QTimer timer_;
    AudioCapture audio_;
    bool audioEnabled_ = true;
    double audioGain_ = 2.0;
    quint32 audioSequence_ = 0;
    adpcm_channel_t adpcm_[2]{};
    stream::PictureSizeControl pictureSize_;
    stream::RumbleDriver rumble_;
    QTimer reconnectTimer_;
    QString lastPort_;
    int reconnectTries_ = 0;
    bool resume_ = false; // a stream was running when the connection was lost
    logger::SessionLog log_;
    stream::StreamStats stats_;
    stream::DeliveryTracker delivery_;
    bool deliveryProfile_ = false;
    pacing::Trace pacingTrace_{"host"};
    QString status_ = "Disconnected", error_;
    bool halting_ = false;
    quint32 sequence_ = 0;
    qint64 beginAt_ = 0, encodeAt_ = 0, startedAt_ = 0, stoppedAt_ = 0, lastWrite_ = 0,
           lastTick_ = 0, lastNotify_ = 0;
    qint64 encoderWriteAt_ = 0;
    bool submittedIdr_ = false;
    stream::CaptureModel capture_;
    stream::FramePipeline pipeline_;
    QSize captureSize_;
};
