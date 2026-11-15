#pragma once
#include "native_serial_writer.h"
#include "serial_write_queue.h"
#include <QSerialPort>
#include <QSerialPortInfo>

inline int bridgePortScore(const QSerialPortInfo& port)
{
    const auto text = (port.description() + ' ' + port.manufacturer()).toLower();
    int score = 0;
    if (port.hasVendorIdentifier() && port.vendorIdentifier() == 0x303A) // Espressif
        score += 100;
    if (text.contains("jtag") || text.contains("espressif") || text.contains("esp32"))
        score += 50;
    return score;
}

class DeviceTransport : public QObject {
    Q_OBJECT
  public:
    DeviceTransport();
    ~DeviceTransport() override;
    bool open(const QString& port);
    void close();
    bool resetOutput();
    bool isOpen() const { return serial_.isOpen(); }
    QString portName() const { return serial_.portName(); }
    QString errorString() const { return error_; }
    void setProfiling(bool enabled) { queue_.setProfiling(enabled); }
    bool enqueue(const QByteArray& bytes, qint64 tag = -1, bool urgent = false);
    bool busy() const { return queue_.busy(); }
    bool drain(int timeoutMs) { return queue_.drain(timeoutMs); } // final destruction/CLI only
    void clearInput() { serial_.clear(QSerialPort::Input); }
    bool setRequestToSend(bool enabled);
    bool resetPulse();
  signals:
    void received(const QByteArray& bytes);
    void failed(const QString& message);
    void recordWritten(qint64 tag, qint64 queueUs, qint64 writeUs);
    void resetFinished();

  private:
    void report(const QString& message);
    QSerialPort serial_;
    NativeSerialWriter native_{serial_};
    SerialWriteQueue queue_{native_};
    QString error_;
    quint64 generation_ = 0;
    bool changing_ = false, resetting_ = false;
};
