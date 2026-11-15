#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>

namespace stream {
class EncoderSession : public QObject {
    Q_OBJECT
  public:
    explicit EncoderSession(QObject* parent = nullptr);
    ~EncoderSession() override;
    bool start(const QString& program, const QStringList& arguments = {}, quint64 epoch = 0,
               int qp = 0, const QString& preset = {});
    bool submit(const QByteArray& pixels, bool idr, int qp = 0);
    void stop();
    bool running() const
    {
        return process_ && process_->state() == QProcess::Running && !stopping_;
    }
    bool idle() const { return !process_; }
    bool busy() const { return busy_; }
  signals:
    void started(quint64 epoch);
    void stopped(quint64 epoch);
    void recordReady(const QByteArray& record, bool idr, quint64 epoch);
    void diagnostic(const QByteArray& bytes);
    void failed(const QString& message, quint64 epoch);

  private:
    void output(QProcess* process);
    void reject(const QString& message);
    QProcess* process_ = nullptr;
    QTimer timeout_;
    QByteArray output_, errors_;
    bool busy_ = false, idr_ = false, stopping_ = false;
    quint64 epoch_ = 0;
};
} // namespace stream
