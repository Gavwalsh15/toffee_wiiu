#pragma once
#include <QByteArray>
#include <QImage>

namespace video {
struct PreparedCapture {
    QImage image;      // the picture as the stream shows it
    QByteArray pixels; // the same picture as I420, ready to encode
    qint64 sourceUs = 0, convertUs = 0;
    bool reused = false;
    QSize sourceSize;
};

QByteArray i420(const QImage& image);

class CaptureConverter {
  public:
    PreparedCapture prepare(const QImage& image, quint64 serial);

  private:
    PreparedCapture cached_;
    quint64 serial_ = 0;
};
} // namespace video
