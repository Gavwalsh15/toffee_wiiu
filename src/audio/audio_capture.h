#pragma once
#include "common/wire_limits.h"
#include <QByteArray>
#include <QObject>
#include <QString>
#include <algorithm>
#include <atomic>
#include <thread>

class AudioCapture : public QObject {
    Q_OBJECT
  public:
    explicit AudioCapture(QObject* parent = nullptr);
    ~AudioCapture() override;
    void start();
    void stop();
    bool running() const { return thread_.joinable(); }
    void setGain(double gain) { gain_ = float(std::clamp(gain, 0.25, 6.0)); }
  signals:
    void chunk(const QByteArray& pcm);
    void failed(const QString& message);

  private:
    void run();
    std::thread thread_;
    std::atomic_bool stop_{false};
    std::atomic<float> gain_{1.0f};
};
