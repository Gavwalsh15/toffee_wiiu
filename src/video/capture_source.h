#pragma once
#include <QImage>
#include <QMutex>
#include <QPointer>
#include <QScreen>
#include <QVariantList>
#include "native_capture.h"

namespace video {
class CaptureSource : public QObject {
    Q_OBJECT
  public:
    explicit CaptureSource(QObject* parent = nullptr);
    ~CaptureSource() override;
    QVariantList refresh(int mode);
    bool select(int mode, int index);
    void stop();
    bool hasFrame() const;
    struct Snapshot {
        QImage image;
        quint64 serial;
        qint64 publishedUs = 0;
    };
    Snapshot snapshot() const;
    static QImage fit(const QImage& image);
  signals:
    void failed(const QString& message);

  private:
    void publish(QImage image);
    void startNative(const QString& device, NativeCapture::Crop crop = {});
    void startNativeCapture(QScreen* screen);
    bool startWindowCapture(quintptr window);
    QList<QPointer<QScreen>> screens_;
    QList<quintptr> windows_;
    mutable QMutex mutex_;
    QImage latest_;
    quint64 serial_ = 0;
    qint64 publishedUs_ = 0;
    NativeCapture native_;
    quint64 nativeGeneration_ = 0;
};
} // namespace video
