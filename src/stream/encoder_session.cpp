#include "encoder_session.h"
#include "protocol.h"
#include "common/video_profile.h"
#include <QProcessEnvironment>

namespace stream {
EncoderSession::EncoderSession(QObject* parent) : QObject(parent)
{
    timeout_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if (!process_)
            return;
        if (stopping_)
            process_->kill();
        else
            reject(busy_ ? "Encoder frame timed out" : "Encoder startup timed out");
    });
}
EncoderSession::~EncoderSession()
{
    if (process_)
    {
        process_->disconnect(this);
        process_->kill();
        process_->waitForFinished(1000);
    }
}
bool EncoderSession::start(const QString& program, const QStringList& arguments, quint64 epoch,
                           int qp, const QString& preset)
{
    if (process_)
        return false;
    output_.clear();
    errors_.clear();
    busy_ = stopping_ = false;
    epoch_ = epoch;
    auto* process = new QProcess(this);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("TOFFEE_ENCODER_QP"));
    environment.remove(QStringLiteral("TOFFEE_ENCODER_PRESET"));
    if (qp > 0)
        environment.insert(QStringLiteral("TOFFEE_ENCODER_QP"), QString::number(qp));
    if (!preset.isEmpty())
        environment.insert(QStringLiteral("TOFFEE_ENCODER_PRESET"), preset);
    process->setProcessEnvironment(environment);
    process_ = process;
    connect(process, &QProcess::started, this, [this, process] {
        if (process != process_ || stopping_)
            return;
        timeout_.stop();
        emit started(epoch_);
    });
    connect(process, &QProcess::readyReadStandardOutput, this,
            [this, process] { output(process); });
    connect(process, &QProcess::readyReadStandardError, this, [this, process] {
        const auto bytes = process->readAllStandardError();
        if (process != process_)
            return;
        errors_ = (errors_ + bytes).right(4096);
        emit diagnostic(bytes);
    });
    auto retire = [this, process] {
        if (process != process_)
            return;
        timeout_.stop();
        process_ = nullptr;
        busy_ = false;
        process->deleteLater();
        emit stopped(epoch_);
    };
    connect(process, &QProcess::errorOccurred, this,
            [this, process, retire](QProcess::ProcessError error) {
                if (process != process_)
                    return;
                if (!stopping_)
                    reject("Encoder: " + process->errorString());
                if (error == QProcess::FailedToStart)
                    retire();
            });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, retire](int, QProcess::ExitStatus) {
                if (process != process_)
                    return;
                if (!stopping_)
                    reject("Encoder stopped: " + QString::fromUtf8(errors_).trimmed());
                retire();
            });
    timeout_.start(3000);
    process->start(program, arguments);
    return true;
}
bool EncoderSession::submit(const QByteArray& pixels, bool idr, int qp)
{
    if (!running() || busy_ || pixels.size() != toffee::video::I420_BYTES || qp < 0 ||
        qp > toffee::video::MAX_PICTURE_QP)
        return false;
    busy_ = true;
    idr_ = idr;
    output_.clear();
    timeout_.start(2000);
    // The flag byte: bit 0 asks for an IDR, the rest carries the QP.
    QByteArray input(1, char(qp << 1 | int(idr)));
    input += pixels;
    if (process_->write(input) != input.size())
    {
        reject("Encoder input write failed");
        return false;
    }
    return true;
}
void EncoderSession::output(QProcess* process)
{
    const auto bytes = process->readAllStandardOutput();
    if (process != process_ || stopping_)
        return;
    if (!busy_)
    {
        if (!bytes.isEmpty())
            reject("Unexpected encoder output");
        return;
    }
    output_ += bytes;
    if (output_.size() < 4)
        return;
    const auto size = u32(output_.constData());
    if (size < DRH_RECORD_OVERHEAD || size > toffee::video::MAX_RECORD_BYTES ||
        output_.size() > qint64(size) + 4)
    {
        reject("Invalid encoder output size");
        return;
    }
    if (output_.size() < qint64(size) + 4)
        return;
    auto record = output_.mid(4);
    output_.clear();
    if (!idr_ && validRecord(record, true))
        idr_ = true;
    if (!validRecord(record, idr_))
    {
        reject("Invalid DRH record or IDR mismatch");
        return;
    }
    busy_ = false;
    timeout_.stop();
    emit recordReady(record, idr_, epoch_);
}
void EncoderSession::reject(const QString& message)
{
    if (stopping_)
        return;
    stop();
    emit failed(message, epoch_);
}
void EncoderSession::stop()
{
    if (!process_ || stopping_)
        return;
    stopping_ = true;
    busy_ = false;
    output_.clear();
    process_->closeWriteChannel();
    timeout_.start(500);
}
} // namespace stream
