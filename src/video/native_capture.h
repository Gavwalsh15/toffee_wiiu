#pragma once
#include <QImage>
#include <QString>
#include <QRect>
#include <QSize>
#include <functional>
#include <memory>

namespace video {
class NativeCapture {
  public:
    NativeCapture();
    ~NativeCapture();
    using Crop = std::function<bool(QRect&)>;
    // device is the display's Windows name, such as \\.\DISPLAY2 (QScreen::name() returns it).
    void start(const QString& device, std::function<void(QImage)> frame,
               std::function<void(QString)> error, Crop crop = {});
    void stop();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace video
