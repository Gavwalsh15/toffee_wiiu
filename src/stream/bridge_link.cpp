#include "bridge_link.h"
#include <QScopedValueRollback>

namespace stream {
BridgeLink::BridgeLink(const QElapsedTimer& clock, QObject* parent)
    : QObject(parent), clock_(clock)
{
    connect(&transport_, &DeviceTransport::received, this, &BridgeLink::receive);
    connect(&transport_, &DeviceTransport::failed, this, [this](const QString& message) {
        if (isOpen() && !closing_)
            emit failed(message);
    });
    connect(&transport_, &DeviceTransport::resetFinished, this, [this] {
        helloWaitSince_ = now();
        emit resetFinished();
    });
    connect(&transport_, &DeviceTransport::recordWritten, this, &BridgeLink::recordWritten);
}

bool BridgeLink::open(const QString& port)
{
    if (!transport_.open(port))
        return false;
    lastReceived_ = now();
    return true;
}

void BridgeLink::close()
{
    QScopedValueRollback<bool> guard(closing_, true);
    transport_.close();
}

void BridgeLink::forget()
{
    feedback_ = {};
    helloWaitSince_ = now();
}

void BridgeLink::receive(const QByteArray& data)
{
    if (data.isEmpty())
        return;
    lastReceived_ = now();
    emit receiving();
    const auto boots = feedback_.boots;
    const bool wasReady = feedback_.ready;
    feedback_.consume(data);
    if (!feedback_.text.isEmpty())
        emit text(feedback_.text);
    if (!feedback_.incompatible.isEmpty())
    {
        emit incompatible(feedback_.incompatible);
        return;
    }
    if (!feedback_.error.isEmpty())
    {
        emit errorReported(feedback_.error);
        return;
    }
    for (const auto& report : feedback_.inputs)
        emit inputReport(report.sequence, report.bytes);
    if (boots != feedback_.boots || (wasReady && !feedback_.ready))
        emit lost();
    emit updated(wasReady);
}
} // namespace stream
