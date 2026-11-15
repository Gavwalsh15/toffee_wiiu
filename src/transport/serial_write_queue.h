#pragma once
#include "common/wire_limits.h"
#include "native_serial_writer.h"
#include <QByteArray>
#include <QIODevice>
#include <QObject>
#include <QString>
#include <QElapsedTimer>
#include <QQueue>

class SerialWriteQueue : public QObject {
    Q_OBJECT
  public:
    static constexpr qsizetype AudioHeadroom = 16384;
    static constexpr qsizetype Capacity =
        MAX_OUTSTANDING_RECORDS * (MAX_USB_PAYLOAD + 20) + 72 + AudioHeadroom;
    explicit SerialWriteQueue(QIODevice& device);
    bool enqueue(const QByteArray& bytes, qint64 tag = -1, bool urgent = false);
    void setProfiling(bool enabled) { profiling_ = enabled; }
    bool busy() const { return offset_ < pending_.size() || device_.bytesToWrite() > 0; }
    bool drain(int timeoutMs);
    void clear();
    QString error() const { return error_; }
  signals:
    void failed(const QString& message);
    void recordWritten(qint64 tag, qint64 queueUs, qint64 writeUs);

  private:
    void pump();
    void reject(const QString& message);
    void completed(qint64 bytes);
    struct Marker {
        quint64 begin, end;
        qint64 tag, queuedAt, startedAt = -1;
    };
    QQueue<Marker> markers_;
    QElapsedTimer profileClock_;
    quint64 enqueuedBytes_ = 0, submittedBytes_ = 0, completedBytes_ = 0;
    bool profiling_ = false;
    QIODevice& device_;
    QByteArray pending_;
    QList<qsizetype> ends_;
    bool headStarts_ = true; // pending_ begins with a record nothing of which was written
    qsizetype offset_ = 0;
    QString error_;
    bool pumping_ = false;
};
