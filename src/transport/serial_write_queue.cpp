#include "serial_write_queue.h"
#include <QDeadlineTimer>
#include <QScopedValueRollback>
#include <algorithm>

SerialWriteQueue::SerialWriteQueue(QIODevice& device) : device_(device)
{
    profileClock_.start();
    connect(&device_, &QIODevice::bytesWritten, this, [this](qint64 bytes) {
        completed(bytes);
        pump();
    });
}

void SerialWriteQueue::reject(const QString& message)
{
    if (!error_.isEmpty())
        return;
    error_ = message;
    emit failed(message);
}

bool SerialWriteQueue::enqueue(const QByteArray& bytes, qint64 tag, bool urgent)
{
    if (!error_.isEmpty())
        return false;
    if (!device_.isOpen())
    {
        reject("USB port is closed");
        return false;
    }
    if (bytes.size() > Capacity - (pending_.size() - offset_) - device_.bytesToWrite())
    {
        reject("USB write queue capacity exceeded");
        return false;
    }
    if (bytes.isEmpty())
        return true;
    if (offset_)
    {
        headStarts_ = ends_.contains(offset_);
        pending_.remove(0, offset_);
        for (auto& end : ends_)
            end -= offset_;
        ends_.removeIf([](qsizetype end) { return end <= 0; });
        offset_ = 0;
    }
    qsizetype at = pending_.size();
    if (urgent && (headStarts_ || pending_.isEmpty()))
        at = 0;
    else if (urgent && !ends_.isEmpty())
        at = ends_.first();
    if (profiling_)
    {
        const quint64 begin = submittedBytes_ + quint64(at - offset_);
        auto marker = markers_.begin();
        while (marker != markers_.end() && marker->begin < begin)
            ++marker;
        for (auto later = marker; later != markers_.end(); ++later)
            later->begin += quint64(bytes.size()), later->end += quint64(bytes.size());
        markers_.insert(marker, {begin, begin + quint64(bytes.size()), tag,
                                 profileClock_.nsecsElapsed() / 1000});
        enqueuedBytes_ += quint64(bytes.size());
    }
    pending_.insert(at, bytes);
    auto end = ends_.begin();
    while (end != ends_.end() && *end <= at)
        ++end;
    for (auto later = end; later != ends_.end(); ++later)
        *later += bytes.size();
    ends_.insert(end, at + bytes.size());
    pump();
    return error_.isEmpty();
}

void SerialWriteQueue::pump()
{
    if (pumping_ || !error_.isEmpty() || !device_.isOpen() || device_.bytesToWrite() > 0 ||
        offset_ == pending_.size())
        return;
    QScopedValueRollback<bool> guard(pumping_, true);
    const auto size = std::min(qsizetype(usb::WorkerBlockBytes), pending_.size() - offset_);
    if (profiling_)
    {
        const auto now = profileClock_.nsecsElapsed() / 1000;
        for (auto& marker : markers_)
            if (marker.startedAt < 0 && marker.begin < submittedBytes_ + quint64(size))
                marker.startedAt = now;
        submittedBytes_ += quint64(size);
    }
    if (device_.write(pending_.constData() + offset_, size) != size)
    {
        reject("USB batch write failed: " + device_.errorString());
        return;
    }
    offset_ += size;
    if (offset_ == pending_.size())
    {
        pending_.clear();
        ends_.clear();
        headStarts_ = true;
        offset_ = 0;
    }
    if (offset_ < pending_.size() && device_.bytesToWrite() == 0)
        QMetaObject::invokeMethod(this, [this] { pump(); }, Qt::QueuedConnection);
}

void SerialWriteQueue::completed(qint64 bytes)
{
    if (!profiling_ || bytes <= 0)
        return;
    completedBytes_ = std::min(submittedBytes_, completedBytes_ + quint64(bytes));
    const auto now = profileClock_.nsecsElapsed() / 1000;
    while (!markers_.isEmpty() && markers_.head().end <= completedBytes_)
    {
        const auto marker = markers_.dequeue();
        if (marker.tag >= 0)
            emit recordWritten(marker.tag, marker.startedAt - marker.queuedAt,
                               now - marker.startedAt);
    }
}

bool SerialWriteQueue::drain(int timeoutMs)
{
    QDeadlineTimer deadline(timeoutMs);
    while (busy())
    {
        if (!error_.isEmpty() || !device_.isOpen() || deadline.hasExpired())
            return false;
        pump();
        if (device_.bytesToWrite() > 0 &&
            !device_.waitForBytesWritten(int(deadline.remainingTime())))
            return false;
    }
    return error_.isEmpty();
}

void SerialWriteQueue::clear()
{
    pending_.clear();
    ends_.clear();
    headStarts_ = true;
    offset_ = 0;
    error_.clear();
    markers_.clear();
    enqueuedBytes_ = submittedBytes_ = completedBytes_ = 0;
}
