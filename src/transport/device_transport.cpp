#include "device_transport.h"
#include <QScopedValueRollback>
#include <QTimer>

DeviceTransport::DeviceTransport()
{
    connect(&serial_, &QSerialPort::readyRead, this, [this] {
        const auto bytes = serial_.readAll();
        if (!bytes.isEmpty())
            emit received(bytes);
    });
    connect(&queue_, &SerialWriteQueue::recordWritten, this, &DeviceTransport::recordWritten);
    connect(&queue_, &SerialWriteQueue::failed, this, &DeviceTransport::report);
    connect(&native_, &NativeSerialWriter::failed, this, &DeviceTransport::report);
    connect(&serial_, &QSerialPort::errorOccurred, this,
            [this](QSerialPort::SerialPortError error) {
                if (error != QSerialPort::NoError && error != QSerialPort::NotOpenError)
                    report("Serial: " + serial_.errorString());
            });
}
DeviceTransport::~DeviceTransport() { close(); }
void DeviceTransport::report(const QString& message)
{
    if (changing_)
        return;
    error_ = message;
    const auto generation = generation_;
    QMetaObject::invokeMethod(
        this,
        [this, generation, message] {
            if (generation == generation_ && isOpen())
                emit failed(message);
        },
        Qt::QueuedConnection);
}
bool DeviceTransport::open(const QString& port)
{
    if (isOpen())
        return false;
    QScopedValueRollback<bool> guard(changing_, true);
    ++generation_;
    error_.clear();
    queue_.clear();
    serial_.setPortName(port);
    serial_.setBaudRate(115200);
    serial_.setDataBits(QSerialPort::Data8);
    serial_.setParity(QSerialPort::NoParity);
    serial_.setStopBits(QSerialPort::OneStop);
    serial_.setFlowControl(QSerialPort::NoFlowControl);
    if (!serial_.open(QIODevice::ReadWrite))
    {
        error_ = "Cannot open " + port + ": " + serial_.errorString();
        return false;
    }
    serial_.setDataTerminalReady(false);
    serial_.setRequestToSend(false);
    clearInput();
    if (!native_.openSession())
    {
        error_ = native_.errorString();
        close();
        return false;
    }
    return true;
}
void DeviceTransport::close()
{
    QScopedValueRollback<bool> guard(changing_, true);
    ++generation_;
    native_.closeSession();
    if (resetting_ && serial_.isOpen())
        serial_.setRequestToSend(false);
    resetting_ = false;
    if (serial_.isOpen())
        serial_.close();
    queue_.clear();
}
bool DeviceTransport::resetOutput()
{
    if (!isOpen())
        return false;
    QScopedValueRollback<bool> guard(changing_, true);
    ++generation_;
    native_.closeSession();
    queue_.clear();
    serial_.clear(QSerialPort::Output);
    if (native_.openSession())
        return true;
    error_ = "USB recovery failed: " + native_.errorString();
    close();
    return false;
}
bool DeviceTransport::enqueue(const QByteArray& bytes, qint64 tag, bool urgent)
{
    if (queue_.enqueue(bytes, tag, urgent))
        return true;
    error_ = queue_.error();
    return false;
}
bool DeviceTransport::setRequestToSend(bool enabled)
{
    if (serial_.setRequestToSend(enabled))
        return true;
    error_ = serial_.errorString();
    return false;
}
bool DeviceTransport::resetPulse()
{
    if (!isOpen() || resetting_ || busy())
        return false;
    if (!setRequestToSend(true))
        return false;
    resetting_ = true;
    const auto generation = generation_;
    QTimer::singleShot(200, this, [this, generation] {
        if (generation != generation_ || !isOpen())
            return;
        resetting_ = false;
        if (!setRequestToSend(false))
        {
            report("Could not release ESP reset: " + error_);
            return;
        }
        emit resetFinished();
    });
    return true;
}
