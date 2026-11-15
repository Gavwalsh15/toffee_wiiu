#include "virtual_display.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "common/elevated_install.h"

namespace {
constexpr int RefreshRate = 60;
const QSize Sizes[] = {{854, 480}, {1280, 720}, {1920, 1080}};
const char* const DriverFiles[] = {"MttVDD.inf", "MttVDD.dll", "mttvdd.cat", "vdd_settings.xml"};

struct Adapter {
    QString name;
    bool attached = false;
};

QList<Adapter> findAdapters()
{
    QList<Adapter> found;
    DISPLAY_DEVICEW device{};
    device.cb = sizeof(device);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &device, 0); ++i)
    {
        if (QString::fromWCharArray(device.DeviceString)
                .contains("Virtual Display Driver", Qt::CaseInsensitive))
            found.append({QString::fromWCharArray(device.DeviceName),
                          (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0});
        device = DISPLAY_DEVICEW{};
        device.cb = sizeof(device);
    }
    return found;
}

bool settingsOutdated()
{
    QFile installed("C:/VirtualDisplayDriver/vdd_settings.xml");
    QFile bundled(":/display-driver/vdd_settings.xml");
    if (!installed.open(QIODevice::ReadOnly) || !bundled.open(QIODevice::ReadOnly))
        return false;
    auto normal = [](QByteArray text) { return text.replace("\r\n", "\n").trimmed(); };
    return normal(installed.readAll()) != normal(bundled.readAll());
}

QString unpackFolder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath("ToffeeDisplayDriver");
}

void reloadDriver()
{
    const HANDLE pipe = CreateFileW(L"\\\\.\\pipe\\MTTVirtualDisplayPipe", GENERIC_WRITE, 0,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
        return;
    const wchar_t command[] = L"RELOAD_DRIVER";
    DWORD written = 0;
    WriteFile(pipe, command, sizeof(command), &written, nullptr);
    CloseHandle(pipe);
}

LONG applyMode(const QString& adapter, DEVMODEW mode)
{
    const auto name = reinterpret_cast<LPCWSTR>(adapter.utf16());
    if (const LONG result = ChangeDisplaySettingsExW(name, &mode, nullptr,
                                                     CDS_UPDATEREGISTRY | CDS_NORESET, nullptr);
        result != DISP_CHANGE_SUCCESSFUL)
        return result;
    return ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
}
} // namespace

void VirtualDisplay::detect()
{
    const auto adapters = findAdapters();
    adapter_.clear();
    active_ = false;
    for (const auto& adapter : adapters)
        if (adapter_.isEmpty() || (adapter.attached && !active_))
        {
            adapter_ = adapter.name;
            active_ = adapter.attached;
        }
    duplicates_ = adapters.size() > 1;
    outdated_ = !adapter_.isEmpty() && settingsOutdated();
    area_ = {};
    if (active_)
    {
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(adapter_.utf16()),
                                   ENUM_CURRENT_SETTINGS, &mode, 0))
            area_ = QRect(mode.dmPosition.x, mode.dmPosition.y, int(mode.dmPelsWidth),
                          int(mode.dmPelsHeight));
    }
}

void VirtualDisplay::attach()
{
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    mode.dmFields =
        DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
    mode.dmBitsPerPel = 32;
    const QSize wanted = Sizes[size_];
    mode.dmPelsWidth = wanted.width();
    mode.dmPelsHeight = wanted.height();
    mode.dmDisplayFrequency = RefreshRate;
    mode.dmPosition.x = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    mode.dmPosition.y = 0;
    LONG result = applyMode(adapter_, mode);
    if (result != 0)
    {
        // Let Windows pick the refresh rate if it does not offer exactly this one
        mode.dmFields &= ~DWORD(DM_DISPLAYFREQUENCY);
        result = applyMode(adapter_, mode);
    }
    if (result != 0)
        error_ = QString("Windows would not turn the monitor on (code %1, %2). Open Display "
                         "settings to check it.")
                     .arg(result)
                     .arg(adapter_);
    else
        setOwned(true);
}

void VirtualDisplay::detach()
{
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    mode.dmFields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;
    bool failed = false;
    for (const auto& adapter : findAdapters())
        if (adapter.attached)
            if (const LONG result = applyMode(adapter.name, mode); result != 0)
            {
                failed = true;
                error_ = QString("Windows would not turn the monitor off (code %1). Open Display "
                                 "settings to check it.")
                             .arg(result);
            }
    if (!failed)
        setOwned(false);
}

void VirtualDisplay::installDriver()
{
    if (installing_)
        return;
    error_.clear();
    const QString folder = unpackFolder();
    QDir().mkpath(folder);
    for (const char* file : DriverFiles)
    {
        if (!unpackResource(QString(":/display-driver/") + file, QDir(folder).filePath(file)))
        {
            error_ = "Could not unpack the driver files.";
            return emit changed();
        }
    }
    const HANDLE process = runElevated(
        QCoreApplication::applicationFilePath(),
        QString("--install-display-driver \"%1\"").arg(QDir::toNativeSeparators(folder)));
    if (!process)
    {
        if (GetLastError() != ERROR_CANCELLED)
            error_ = "Could not start the installer.";
        return emit changed();
    }
    installerProcess_ = process;
    installing_ = true;
    installWatch_.start();
    emit changed();
}

void VirtualDisplay::watchInstaller()
{
    const auto process = static_cast<HANDLE>(installerProcess_);
    DWORD code = 0;
    if (!process || !GetExitCodeProcess(process, &code) || code == STILL_ACTIVE)
        return;
    installWatch_.stop();
    CloseHandle(process);
    installerProcess_ = nullptr;
    installing_ = false;
    QDir(unpackFolder()).removeRecursively();
    setOwned(true);
    if (code != 0)
        error_ = QString("The driver installer failed (error 0x%1).").arg(quint32(code), 0, 16);
    reloadDriver();
    QTimer::singleShot(2500, this, &VirtualDisplay::refresh);
    emit changed();
}

namespace {
const char* const OwnedKey = "Display/monitorOwnedByToffee";
}

bool VirtualDisplay::owned() const { return QSettings().value(OwnedKey, false).toBool(); }
void VirtualDisplay::setOwned(bool value) { QSettings().setValue(OwnedKey, value); }

VirtualDisplay::VirtualDisplay(QObject* parent) : QObject(parent)
{
    installWatch_.setInterval(500);
    connect(&installWatch_, &QTimer::timeout, this, &VirtualDisplay::watchInstaller);
    detect();
    connect(qApp, &QCoreApplication::aboutToQuit, this, &VirtualDisplay::release);
    connect(qGuiApp, &QGuiApplication::commitDataRequest, this, &VirtualDisplay::release);
    // A monitor left over from a run that crashed or was killed.
    QTimer::singleShot(0, this, &VirtualDisplay::refresh);
}

VirtualDisplay::~VirtualDisplay() { release(); }

void VirtualDisplay::release()
{
    installWatch_.stop();
    if (installerProcess_)
    {
        CloseHandle(static_cast<HANDLE>(installerProcess_));
        installerProcess_ = nullptr;
    }
    detect();
    if (active_ && owned())
        detach();
    if (!active_)
        setOwned(false);
}

void VirtualDisplay::setEnabled(bool value)
{
    if (value == enabled_)
        return;
    enabled_ = value;
    error_.clear();
    refresh();
}

void VirtualDisplay::setSize(int value)
{
    value = qBound(0, value, 2);
    if (value == size_)
        return;
    size_ = value;
    error_.clear();
    if (active_)
    {
        attach();
        detect();
    }
    emit changed();
}

void VirtualDisplay::refresh()
{
    detect();
    if (enabled_ && installed() && !active_)
        attach();
    else if (!enabled_ && active_ && owned())
        detach();
    detect();
    if (enabled_ && active_)
        setOwned(true);
    emit changed();
}

QString VirtualDisplay::status() const
{
    if (installing_)
        return QStringLiteral("Installing the driver… finish the installer window");
    if (!error_.isEmpty())
        return error_;
    if (!installed())
        return QStringLiteral("The virtual display driver is not installed");
    if (duplicates_)
        return QStringLiteral("An extra virtual monitor device was found: update to remove it");
    if (outdated_)
        return QStringLiteral(
            "The monitor settings are out of date: update them to get every size");
    if (active_)
        return QStringLiteral("On · shown on the GamePad when you capture it");
    return enabled_ ? QStringLiteral("Turning on…") : QStringLiteral("Off");
}
