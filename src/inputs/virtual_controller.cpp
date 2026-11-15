#include "virtual_controller.h"
#include "gamepad_buttons.h"
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QStandardPaths>
#include <algorithm>
#include <cstring>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "common/elevated_install.h"
#include <ViGEm/Client.h>

struct VirtualController::Impl {
    PVIGEM_CLIENT client = nullptr;
    PVIGEM_TARGET target = nullptr;
    XUSB_REPORT last{};
    bool sent = false;
};

namespace {
using namespace gamepad;

SHORT stick(const QVariantList& axes, int index)
{
    const double value = index < axes.size() ? axes[index].toDouble() : 0.0;
    return SHORT(std::clamp(value, -1.0, 1.0) * 32767.0);
}

XUSB_REPORT makeReport(quint32 buttons, const QVariantList& axes, bool xboxLayout)
{
    struct Mapping {
        quint32 pad;
        USHORT xbox;
    };
    const Mapping face[] = {
        {PadA, USHORT(xboxLayout ? XUSB_GAMEPAD_B : XUSB_GAMEPAD_A)},
        {PadB, USHORT(xboxLayout ? XUSB_GAMEPAD_A : XUSB_GAMEPAD_B)},
        {PadX, USHORT(xboxLayout ? XUSB_GAMEPAD_Y : XUSB_GAMEPAD_X)},
        {PadY, USHORT(xboxLayout ? XUSB_GAMEPAD_X : XUSB_GAMEPAD_Y)},
    };
    const Mapping others[] = {
        {Up, XUSB_GAMEPAD_DPAD_UP},      {Down, XUSB_GAMEPAD_DPAD_DOWN},
        {Left, XUSB_GAMEPAD_DPAD_LEFT},  {Right, XUSB_GAMEPAD_DPAD_RIGHT},
        {L, XUSB_GAMEPAD_LEFT_SHOULDER}, {R, XUSB_GAMEPAD_RIGHT_SHOULDER},
        {L3, XUSB_GAMEPAD_LEFT_THUMB},   {R3, XUSB_GAMEPAD_RIGHT_THUMB},
        {Plus, XUSB_GAMEPAD_START},      {Minus, XUSB_GAMEPAD_BACK},
        {Home, XUSB_GAMEPAD_GUIDE},
    };
    XUSB_REPORT report{};
    for (const auto& m : face)
        if (buttons & m.pad)
            report.wButtons |= m.xbox;
    for (const auto& m : others)
        if (buttons & m.pad)
            report.wButtons |= m.xbox;
    // ZL and ZR are buttons on the GamePad Xbox reads them as triggers.
    report.bLeftTrigger = (buttons & ZL) ? 255 : 0;
    report.bRightTrigger = (buttons & ZR) ? 255 : 0;
    report.sThumbLX = stick(axes, 0);
    report.sThumbLY = stick(axes, 1);
    report.sThumbRX = stick(axes, 2);
    report.sThumbRY = stick(axes, 3);
    return report;
}

VOID CALLBACK onRumble(PVIGEM_CLIENT, PVIGEM_TARGET, UCHAR large, UCHAR small, UCHAR, LPVOID user)
{
    auto* self = static_cast<VirtualController*>(user);
    const int strength = std::max<int>(large, small);
    QMetaObject::invokeMethod(
        self, [self, strength] { emit self->rumble(strength); }, Qt::QueuedConnection);
}
} // namespace

void VirtualController::connectBus()
{
    auto impl = std::make_unique<Impl>();
    impl->client = vigem_alloc();
    if (!impl->client)
        return setState(State::Failed);
    if (const auto result = vigem_connect(impl->client); !VIGEM_SUCCESS(result))
    {
        vigem_free(impl->client);
        return setState(result == VIGEM_ERROR_BUS_NOT_FOUND ? State::DriverMissing : State::Failed,
                        int(result));
    }
    impl->target = vigem_target_x360_alloc();
    if (const auto result = vigem_target_add(impl->client, impl->target); !VIGEM_SUCCESS(result))
    {
        vigem_target_free(impl->target);
        vigem_disconnect(impl->client);
        vigem_free(impl->client);
        return setState(State::Failed, int(result));
    }
    vigem_target_x360_register_notification(impl->client, impl->target, onRumble, this);
    impl_ = std::move(impl);
    setState(State::Ready);
}

void VirtualController::disconnectBus()
{
    if (!impl_)
        return;
    vigem_target_x360_unregister_notification(impl_->target);
    vigem_target_remove(impl_->client, impl_->target);
    vigem_target_free(impl_->target);
    vigem_disconnect(impl_->client);
    vigem_free(impl_->client);
    impl_.reset();
    emit rumble(0);
}

void VirtualController::update(quint32 buttons, const QVariantList& axes)
{
    if (!impl_)
        return;
    const auto report = makeReport(buttons, axes, xboxLayout_);
    if (impl_->sent && memcmp(&report, &impl_->last, sizeof(report)) == 0)
        return;
    impl_->last = report;
    impl_->sent = true;
    vigem_target_x360_update(impl_->client, impl_->target, report);
}

VirtualController::VirtualController(QObject* parent) : QObject(parent)
{
    installWatch_.setInterval(2000);
    connect(&installWatch_, &QTimer::timeout, this, [this] {
        retry();
        if (state_ != State::Installing || ++installChecks_ > 60)
        {
            installWatch_.stop();
            if (state_ == State::Installing)
                setState(State::DriverMissing);
        }
    });
}
VirtualController::~VirtualController() { disconnectBus(); }

void VirtualController::setEnabled(bool value)
{
    if (value == enabled_)
        return;
    enabled_ = value;
    if (value)
        connectBus();
    else
    {
        disconnectBus();
        setState(State::Off);
    }
}

void VirtualController::setXboxLayout(bool value)
{
    if (value == xboxLayout_)
        return;
    xboxLayout_ = value;
    release(); // the old layout's pressed buttons must not stay held
}

void VirtualController::retry()
{
    if (!enabled_ || impl_)
        return;
    const auto before = state_;
    connectBus();
    if (before == State::Installing && state_ == State::DriverMissing)
        setState(State::Installing);
}

void VirtualController::installDriver()
{
    if (state_ == State::Installing)
        return;
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                             .filePath("ViGEmBus_setup.exe");
    if (!unpackResource(":/driver/ViGEmBus_setup.exe", path))
        return setState(State::Failed);
    const HANDLE process = runElevated(path);
    if (!process)
        return;
    CloseHandle(process);
    installChecks_ = 0;
    setState(State::Installing);
    installWatch_.start();
}

void VirtualController::release() { update(0, {0.0, 0.0, 0.0, 0.0}); }

void VirtualController::setState(State state, int error)
{
    if (state == state_ && error == error_)
        return;
    state_ = state;
    error_ = error;
    emit stateChanged();
}

QString VirtualController::status() const
{
    switch (state_)
    {
    case State::Off:
        return QStringLiteral("Off");
    case State::Ready:
        return QStringLiteral("Active · Windows sees an Xbox 360 controller");
    case State::DriverMissing:
        return QStringLiteral("The ViGEmBus driver is not installed");
    case State::Installing:
        return QStringLiteral("Installing the driver… finish the installer window");
    case State::Failed:
        break;
    }
    return QStringLiteral("Could not create the controller (error 0x%1)")
        .arg(quint32(error_), 8, 16, QLatin1Char('0'));
}
