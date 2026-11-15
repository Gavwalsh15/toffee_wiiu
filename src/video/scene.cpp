#include "scene.h"
#include "common/video_profile.h"
#include "inputs/gamepad_buttons.h"
#include <QFont>
#include <QPainter>
#include <algorithm>
#include <cmath>
namespace video {
namespace {
const QColor caramel("#f4c38a");
const QColor colours[] = {caramel, QColor("#89baff"), QColor("#91d5ba"), QColor("#c2a7ef")};

void font(QPainter& p, int size, bool bold = false)
{
    QFont f("Segoe UI");
    f.setPixelSize(size);
    f.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    p.setFont(f);
}

QImage background()
{
    QImage image(int(toffee::video::WIDTH), int(toffee::video::HEIGHT), QImage::Format_RGB32);
    image.fill(QColor("#1b1b1b"));
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(caramel);
    p.drawRoundedRect(28, 20, 32, 32, 8, 8);
    p.setPen(QColor("#29211a"));
    font(p, 23, true);
    p.drawText(QRect(28, 18, 32, 32), Qt::AlignCenter, "T");
    p.setPen(QColor("#eeeeee"));
    font(p, 22, true);
    p.drawText(72, 45, "Toffee");
    font(p, 14);
    p.setPen(QColor("#999999"));
    p.drawText(QRect(480, 22, 354, 32), Qt::AlignRight | Qt::AlignVCenter, "Interactive test");
    p.setPen(QColor("#353535"));
    p.drawLine(28, 68, 836, 68);
    p.drawLine(28, 404, 836, 404);
    font(p, 13);
    p.setPen(QColor("#a6a6a6"));
    p.drawText(QRect(28, 419, 808, 24), Qt::AlignLeft | Qt::AlignVCenter,
               "Move / aim / boost / reset / colour");
    font(p, 12);
    p.setPen(QColor("#777777"));
    p.drawText(28, 465, "by gavwalsh15");
    return image;
}
} // namespace

void Scene::advance(double dt, bool active, const QVariantList& axes, quint32 buttons)
{
    using namespace gamepad;
    dt = std::clamp(dt, 0.0, 0.1);
    const quint32 pressed = buttons & ~previousButtons;
    previousButtons = buttons;
    if (pressed & PadX)
        colour = (colour + 1) % 4;
    if (pressed & PadB)
    {
        const Scene start;
        x = start.x;
        y = start.y;
        targetX = start.targetX;
        targetY = start.targetY;
    }
    if (active && axes.size() == 4)
    {
        const double dx = axes[0].toDouble() + ((buttons & Right) ? 1 : 0) - ((buttons & Left) ? 1 : 0);
        const double dy = -axes[1].toDouble() + ((buttons & Down) ? 1 : 0) - ((buttons & Up) ? 1 : 0);
        const double speed = (buttons & PadA) ? 440 : 180;
        x += std::clamp(dx, -1.0, 1.0) * speed * dt;
        y += std::clamp(dy, -1.0, 1.0) * speed * dt;
        targetX += axes[2].toDouble() * 180 * dt;
        targetY -= axes[3].toDouble() * 180 * dt;
    }
    x = std::clamp(x, 28.0, 772.0);
    y = std::clamp(y, 84.0, 324.0);
    targetX = std::clamp(targetX, 44.0, 816.0);
    targetY = std::clamp(targetY, 100.0, 384.0);
}

QImage render(const Scene& state)
{
    static const QImage base = background();
    QImage image = base;
    QPainter p(&image);
    p.setPen(QPen(QColor("#727272"), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(state.targetX - 16, state.targetY - 16, 32, 32));
    p.fillRect(QRectF(state.x, state.y, 64, 64), colours[state.colour % 4]);
    if (std::abs(state.x + 32 - state.targetX) < 48 && std::abs(state.y + 32 - state.targetY) < 48)
    {
        p.setPen(QPen(caramel, 2));
        p.drawRect(QRectF(state.targetX - 20, state.targetY - 20, 40, 40));
    }
    return image;
}
} // namespace video
