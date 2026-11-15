#pragma once
#include <QImage>
#include <QVariantList>
namespace video {
struct Scene {
    enum Mode { Interactive, DisplayCapture, WindowCapture };
    double x = 384, y = 192, targetX = 590, targetY = 270;
    int colour = 0, mode = Interactive;
    quint32 previousButtons = 0;
    bool capturing() const { return mode != Interactive; }
    void advance(double seconds, bool active, const QVariantList& axes, quint32 buttons);
};
QImage render(const Scene& state);
} // namespace video
