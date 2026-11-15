#pragma once
#include <QByteArray>
#include <QString>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace logger {
class SessionLog {
  public:
    static constexpr qsizetype Capacity = 1024 * 1024;
    ~SessionLog() { close(); }
    void setFileName(const QString& path) { path_ = path; }
    QString fileName() const { return path_; }
    bool open();
    bool isOpen() const { return worker_.joinable(); }
    QString errorString() const;
    void write(const QByteArray& bytes);
    void requestClose();
    bool closing() const;
    bool finished() const;
    void close();

  private:
    void run();
    QString path_, error_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    QByteArray pending_;
    quint64 dropped_ = 0;
    bool stopping_ = false, opened_ = false, started_ = false, finished_ = true;
};
} // namespace logger
