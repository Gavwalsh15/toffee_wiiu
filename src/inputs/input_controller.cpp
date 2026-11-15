#include "input_controller.h"
#include "common/bridge_record.h"
#include <QtEndian>
#include <algorithm>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
InputController::InputController(QObject* parent) : QObject(parent)
{
    expiry_.setInterval(50);
    connect(&expiry_, &QTimer::timeout, this, &InputController::expire);
    expiry_.start();
    connect(&virtualPad_, &VirtualController::stateChanged, this, &InputController::changed);
    connect(&virtualPad_, &VirtualController::rumble, this, &InputController::rumbleRequested);
}
InputController::~InputController() { updateMouse(false); }
QVariantList InputController::rawAxes() const { return {raw_[0], raw_[1], raw_[2], raw_[3]}; }
QVariantList InputController::controls() const
{
    QVariantList list;
    for (const auto& control : gamepad::Controls)
        list.append(QVariantMap{{"name", QString::fromUtf8(control.name)}, {"mask", control.mask}});
    return list;
}
QVariantList InputController::axes() const
{
    QVariantList values;
    for (auto raw : raw_)
    {
        double value = (std::clamp<int>(raw, gamepad::StickMin, gamepad::StickMax) -
                        gamepad::StickCentre) / gamepad::StickHalfRange;
        values.append(std::abs(value) <= deadZone_ ? 0.0 : value);
    }
    return values;
}
void InputController::setDeadZone(double value)
{
    value = std::clamp(value, 0.0, 0.3);
    if (value == deadZone_)
        return;
    deadZone_ = value;
    emit changed();
}
void InputController::setTouchAsMouse(bool value)
{
    if (value == touchAsMouse_)
        return;
    touchAsMouse_ = value;
    if (!value)
        updateMouse(false);
    else if (touchActive_)
        updateMouse(true);
    emit changed();
}
void InputController::setRealTouch(bool value)
{
    if (value == realTouch_)
        return;
    realTouch_ = value;
    emit changed();
}
void InputController::setTouchArea(const QRect& value)
{
    if (value == touchArea_)
        return;
    touchArea_ = value;
    emit changed();
}
namespace {
bool injectTouch(bool touching, bool wasTouching, int x, int y)
{
    static const bool ready = InitializeTouchInjection(1, TOUCH_FEEDBACK_NONE);
    if (!ready)
        return false;
    POINTER_TOUCH_INFO contact{};
    contact.pointerInfo.pointerType = PT_TOUCH;
    contact.pointerInfo.pointerId = 0;
    contact.pointerInfo.ptPixelLocation = {x, y};
    contact.pointerInfo.pointerFlags =
        touching ? (POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT |
                    (wasTouching ? POINTER_FLAG_UPDATE : POINTER_FLAG_DOWN))
                 : POINTER_FLAG_UP;
    contact.touchFlags = TOUCH_FLAG_NONE;
    contact.touchMask = TOUCH_MASK_CONTACTAREA | TOUCH_MASK_ORIENTATION | TOUCH_MASK_PRESSURE;
    contact.rcContact = {x - 2, y - 2, x + 2, y + 2};
    contact.orientation = 90;
    contact.pressure = 32;
    return InjectTouchInput(1, &contact);
}
} // namespace
void InputController::updateMouse(bool touchActive)
{
    if (!touchAsMouse_)
        touchActive = false;
    if (!touchActive && !mouseButtonDown_)
        return;

    const QRect desktop(GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                        GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
    const QRect area = touchArea_.isEmpty() ? desktop : touchArea_;
    int x = 0, y = 0;
    if (touchActive)
    {
        constexpr int TouchMinX = 100, TouchMaxX = 3995;
        constexpr int TouchMinY = 200, TouchMaxY = 3895;
        const auto tx = std::clamp(touchX_, TouchMinX, TouchMaxX) - TouchMinX;
        const auto ty = std::clamp(touchY_, TouchMinY, TouchMaxY) - TouchMinY;
        x = area.x() + int(qint64(tx) * (area.width() - 1) / (TouchMaxX - TouchMinX));
        y = area.y() + int(qint64(ty) * (area.height() - 1) / (TouchMaxY - TouchMinY));
    } else
    {
        x = lastX_;
        y = lastY_;
    }
    lastX_ = x;
    lastY_ = y;

    const bool asTouch = mouseButtonDown_ ? pressedAsTouch_ : realTouch_;
    if (asTouch)
    {
        const bool injected = injectTouch(touchActive, mouseButtonDown_, x, y);
        if (injected || mouseButtonDown_)
        {
            pressedAsTouch_ = true;
            mouseButtonDown_ = injected && touchActive;
            return;
        }
    }
    INPUT event{};
    event.type = INPUT_MOUSE;
    if (touchActive)
    {
        event.mi.dx = LONG(qint64(x - desktop.x()) * 65535 / std::max(1, desktop.width() - 1));
        event.mi.dy = LONG(qint64(y - desktop.y()) * 65535 / std::max(1, desktop.height() - 1));
        event.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        if (!mouseButtonDown_)
            event.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
    } else
        event.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &event, sizeof(event));
    pressedAsTouch_ = false;
    mouseButtonDown_ = touchActive;
}
void InputController::clear()
{
    const bool wasActive = active_;
    updateMouse(false);
    active_ = false;
    touchActive_ = false;
    touchX_ = touchY_ = 0;
    std::fill(std::begin(accel_), std::end(accel_), 0);
    std::fill(std::begin(gyro_), std::end(gyro_), 0);
    buttons_ = 0;
    std::fill(std::begin(raw_), std::end(raw_), quint16(gamepad::StickCentre));
    virtualPad_.release();
    if (wasActive)
        emit changed();
}
void InputController::reset()
{
    clear();
    haveSequence_ = false;
    reports_ = 0;
    emit changed();
}
void InputController::expire()
{
    if (active_ && lastReport_.elapsed() > 500)
        clear();
}
// Where the report keeps what we read: buttons high, middle and low bytes, the four sticks, then
// acceleration, rotation and the touch samples.
namespace offset {
constexpr int ButtonsHigh = 80, ButtonsMid = 2, ButtonsLow = 3, Sticks = 6, Accel = 15, Gyro = 21,
              Touch = 36;
} // namespace offset
QByteArray InputController::makeReport(quint32 buttons, const quint16 (&sticks)[4])
{
    QByteArray report(BRIDGE_INPUT_REPORT_BYTES, '\0');
    report[offset::ButtonsHigh] = char(buttons >> 16);
    report[offset::ButtonsMid] = char(buttons >> 8);
    report[offset::ButtonsLow] = char(buttons);
    for (int i = 0; i < 4; ++i)
        qToLittleEndian(sticks[i], report.data() + offset::Sticks + 2 * i);
    // Resting flat: 1 g down the third axis.
    qToLittleEndian(qint16(gamepad::AccelPerG), report.data() + offset::Accel + 4);
    return report;
}
bool InputController::report(quint32 sequence, const QByteArray& report)
{
    if (report.size() != BRIDGE_INPUT_REPORT_BYTES)
        return false;
    if (haveSequence_ && (quint32(sequence - sequence_) >= 0x80000000u || sequence == sequence_))
        return false;
    buttons_ = (quint32(quint8(report[offset::ButtonsHigh])) << 16) |
               (quint32(quint8(report[offset::ButtonsMid])) << 8) |
               quint8(report[offset::ButtonsLow]);
    for (int i = 0; i < 4; ++i)
        raw_[i] = qFromLittleEndian<quint16>(report.constData() + offset::Sticks + 2 * i);
    for (int i = 0; i < 3; ++i)
    {
        accel_[i] = qFromLittleEndian<qint16>(report.constData() + offset::Accel + 2 * i);
        const auto* g = reinterpret_cast<const quint8*>(report.constData()) + offset::Gyro + 3 * i;
        const int value = g[0] | (g[1] << 8) | (g[2] << 16);
        gyro_[i] = (value ^ 0x800000) - 0x800000; // sign-extend 24 bits
    }
    int x = 0, y = 0;
    for (int i = 0; i < 10; ++i)
    {
        const auto sample = offset::Touch + 4 * i;
        x += (quint8(report[sample + 1]) & 0x0f) << 8 | quint8(report[sample]);
        y += (quint8(report[sample + 3]) & 0x0f) << 8 | quint8(report[sample + 2]);
    }
    touchX_ = x / 10;
    touchY_ = 4095 - y / 10;
    const int pressure = ((quint8(report[37]) >> 4) & 7) | (((quint8(report[39]) >> 4) & 7) << 3) |
                         (((quint8(report[41]) >> 4) & 7) << 6) |
                         (((quint8(report[43]) >> 4) & 7) << 9);
    touchActive_ = pressure != 0;
    updateMouse(touchActive_);
    virtualPad_.update(buttons_, axes());
    sequence_ = sequence;
    haveSequence_ = true;
    active_ = true;
    ++reports_;
    lastReport_.restart();
    emit changed();
    return true;
}
void InputController::bridgeLost()
{
    clear();
    haveSequence_ = false;
}
