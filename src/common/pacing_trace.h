#pragma once
#include <QByteArray>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDebug>
#include "monotonic_clock.h"

namespace pacing {
class Trace {
    QFile file_;
    QByteArray pending_;
    qint64 flushedAt_ = 0;

  public:
    explicit Trace(const char* name)
    {
        if (!qEnvironmentVariableIsSet("TOFFEE_PACING_PROFILE"))
            return;
        auto directory =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/logs";
        QDir().mkpath(directory);
        file_.setFileName(directory + "/pacing-" + QString::fromLatin1(name) + "-" +
                          QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") + "-" +
                          QString::number(QCoreApplication::applicationPid()) + ".log");
        if (file_.open(QIODevice::WriteOnly))
            qInfo().noquote() << "PACING_LOG" << file_.fileName();
        else
            qWarning() << "Cannot open pacing trace:" << file_.errorString();
    }
    ~Trace() { flush(); }
    bool enabled() const { return file_.isOpen(); }
    void flush()
    {
        if (!pending_.isEmpty())
        {
            file_.write(pending_);
            pending_.clear();
            file_.flush();
        }
        flushedAt_ = nowUs();
    }
    void add(const QByteArray& row)
    {
        if (!enabled())
            return;
        pending_ += row;
        pending_ += '\n';
        if (pending_.size() >= 65536 || nowUs() - flushedAt_ >= 1000000)
            flush();
    }
};
} // namespace pacing
