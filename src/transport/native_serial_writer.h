#pragma once
#include <QIODevice>
#include <QSerialPort>
#include <memory>

namespace usb {
inline constexpr int PacketBytes = 64;
inline constexpr int WorkerBlockBytes = 8192;
} // namespace usb

class NativeSerialWriter : public QIODevice {
    Q_OBJECT
  public:
    explicit NativeSerialWriter(QSerialPort& port);
    ~NativeSerialWriter() override;
    bool openSession();
    void closeSession();
    qint64 bytesToWrite() const override;
    bool waitForBytesWritten(int timeoutMs) override;
  signals:
    void failed(const QString& message);

  protected:
    qint64 readData(char*, qint64) override { return -1; }
    qint64 writeData(const char* data, qint64 size) override;

  private:
    struct State;
    void finish(quint64 generation);
    QSerialPort& port_;
    std::unique_ptr<State> state_;
    bool failed_ = false;
    quint64 generation_ = 0;
    qint64 inFlight_ = 0;
};
