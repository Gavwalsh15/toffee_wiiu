#include "session_log.h"
#include <QFile>

namespace logger {
bool SessionLog::open()
{
    if (isOpen())
        return false;
    {
        std::lock_guard lock(mutex_);
        pending_.clear();
        error_.clear();
        dropped_ = 0;
        stopping_ = opened_ = started_ = finished_ = false;
    }
    worker_ = std::thread([this] { run(); });
    std::unique_lock lock(mutex_);
    wake_.wait(lock, [this] { return started_; });
    const bool opened = opened_;
    lock.unlock();
    if (!opened)
        close();
    return opened;
}
QString SessionLog::errorString() const
{
    std::lock_guard lock(mutex_);
    return error_;
}
void SessionLog::write(const QByteArray& bytes)
{
    std::lock_guard lock(mutex_);
    if (!opened_ || stopping_ || bytes.isEmpty())
        return;
    if (bytes.size() > Capacity - pending_.size())
        dropped_ += quint64(bytes.size());
    else
        pending_ += bytes;
    wake_.notify_one();
}
void SessionLog::requestClose()
{
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_one();
}
bool SessionLog::closing() const
{
    std::lock_guard lock(mutex_);
    return stopping_;
}
bool SessionLog::finished() const
{
    std::lock_guard lock(mutex_);
    return finished_;
}
void SessionLog::close()
{
    if (worker_.joinable())
    {
        requestClose();
        worker_.join();
    }
}
void SessionLog::run()
{
    QFile file(path_);
    {
        std::lock_guard lock(mutex_);
        opened_ = file.open(QIODevice::WriteOnly);
        started_ = true;
        if (!opened_)
        {
            error_ = file.errorString();
            finished_ = true;
        }
    }
    wake_.notify_one();
    if (!opened_)
        return;
    for (;;)
    {
        QByteArray batch;
        quint64 dropped;
        bool stopping;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !pending_.isEmpty() || dropped_; });
            batch.swap(pending_);
            dropped = dropped_;
            dropped_ = 0;
            stopping = stopping_;
        }
        if (dropped)
            batch += "\nHOST_LOG_DROPPED_BYTES:" + QByteArray::number(dropped) + '\n';
        if (!batch.isEmpty() && (file.write(batch) != batch.size() || !file.flush()))
        {
            file.close();
            std::lock_guard lock(mutex_);
            error_ = file.errorString();
            opened_ = false;
            pending_.clear();
            finished_ = true;
            return;
        }
        if (stopping)
            break;
    }
    file.close();
    std::lock_guard lock(mutex_);
    opened_ = false;
    finished_ = true;
}
} // namespace logger
