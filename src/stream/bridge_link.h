#pragma once
#include "protocol.h"
#include "transport/device_transport.h"
#include <QElapsedTimer>
#include <QObject>
#include <memory>

namespace stream {
class BridgeLink : public QObject {
    Q_OBJECT
  public:
    explicit BridgeLink(const QElapsedTimer& clock, QObject* parent = nullptr);

    bool open(const QString& port);
    void close();
    bool isOpen() const { return transport_.isOpen(); }
    bool closing() const { return closing_; }
    QString portName() const { return transport_.portName(); }
    QString errorString() const { return transport_.errorString(); }

    bool send(const QByteArray& record, qint64 tag = -1, bool urgent = false)
    {
        return transport_.enqueue(record, tag, urgent);
    }
    bool busy() const { return transport_.busy(); }
    bool drain(int timeoutMs) { return transport_.drain(timeoutMs); }
    bool resetOutput() { return transport_.resetOutput(); }
    // resetFinished() follows.
    bool resetPulse() { return transport_.resetPulse(); }
    void clearInput() { transport_.clearInput(); }
    void setProfiling(bool enabled) { transport_.setProfiling(enabled); }

    Feedback& feedback() { return feedback_; }
    const Feedback& feedback() const { return feedback_; }
    void forget();
    qint64 quietFor() const { return now() - lastReceived_; }
    // -1 once the first HELLO has come.
    qint64 waitingForHello() const { return feedback_.hello ? -1 : now() - helloWaitSince_; }
    qint64 lastReceived() const { return lastReceived_; }

  signals:
    void receiving();
    void text(const QByteArray& text);
    void inputReport(quint32 sequence, const QByteArray& report);
    void incompatible(const QString& message);
    void errorReported(const QString& message);
    // The bridge restarted or the GamePad was lost.
    void lost();
    void updated(bool wasReady);
    void failed(const QString& message);
    void resetFinished();
    void recordWritten(qint64 tag, qint64 queueUs, qint64 writeUs);

  private:
    qint64 now() const { return clock_.nsecsElapsed() / 1000; }
    void receive(const QByteArray& data);

    const QElapsedTimer& clock_;
    DeviceTransport transport_;
    Feedback feedback_;
    qint64 lastReceived_ = 0, helloWaitSince_ = 0;
    bool closing_ = false;
};
} // namespace stream
