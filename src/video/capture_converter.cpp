#include "capture_converter.h"
#include "capture_source.h"
#include "common/video_profile.h"
#include <QElapsedTimer>
#include <algorithm>

namespace video {
QByteArray i420(const QImage& image)
{
    const auto rgb = image.convertToFormat(QImage::Format_RGB32);
    Q_ASSERT(rgb.width() == toffee::video::WIDTH && rgb.height() == toffee::video::HEIGHT);
    QByteArray output(toffee::video::I420_BYTES, Qt::Uninitialized);
    auto* out = reinterpret_cast<unsigned char*>(output.data());
    auto clamp = [](int value) { return static_cast<unsigned char>(std::clamp(value, 0, 255)); };
    for (int y = 0; y < int(toffee::video::HEIGHT); y += 2)
    {
        const auto* row0 = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        const auto* row1 = reinterpret_cast<const QRgb*>(rgb.constScanLine(y + 1));
        for (int x = 0; x < int(toffee::video::WIDTH); x += 2)
        {
            int r = 0, g = 0, b = 0;
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 2; ++i)
                {
                    const auto pixel = j ? row1[x + i] : row0[x + i];
                    const int pr = qRed(pixel), pg = qGreen(pixel), pb = qBlue(pixel);
                    out[(y + j) * toffee::video::WIDTH + x + i] =
                        clamp(((66 * pr + 129 * pg + 25 * pb + 128) >> 8) + 16);
                    r += pr;
                    g += pg;
                    b += pb;
                }
            r /= 4;
            g /= 4;
            b /= 4;
            const int c = (y / 2) * toffee::video::CHROMA_WIDTH + x / 2;
            out[toffee::video::U_OFFSET + c] = clamp(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
            out[toffee::video::V_OFFSET + c] = clamp(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
        }
    }
    return output;
}
PreparedCapture CaptureConverter::prepare(const QImage& image, quint64 serial)
{
    if (image.isNull())
    {
        cached_ = {};
        return {};
    }
    if (!cached_.pixels.isEmpty() && serial == serial_)
    {
        auto result = cached_;
        result.sourceUs = result.convertUs = 0;
        result.reused = true;
        return result;
    }
    QElapsedTimer timer;
    timer.start();
    PreparedCapture result;
    result.image = CaptureSource::fit(image);
    result.pixels = i420(result.image);
    result.convertUs = timer.nsecsElapsed() / 1000;
    result.sourceSize = image.size();
    if (!result.pixels.isEmpty())
    {
        cached_ = result;
        serial_ = serial;
    }
    return result;
}
} // namespace video
