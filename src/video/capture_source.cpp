#include "capture_source.h"
#include <QGuiApplication>
#include <QMutexLocker>
#include <QPainter>
#include <QScreen>
#include "common/monotonic_clock.h"
#include "common/video_profile.h"
#include "scene.h"
#include <utility>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <vector>

namespace video {
namespace profile = toffee::video;
CaptureSource::CaptureSource(QObject* parent) : QObject(parent) {}
void CaptureSource::publish(QImage image)
{
    QMutexLocker lock(&mutex_);
    latest_ = std::move(image);
    ++serial_;
    publishedUs_ = pacing::nowUs();
}

namespace {
QString windowsDisplayName(const QScreen* screen)
{
    if (screen->name().startsWith(QLatin1String("\\\\.\\")))
        return screen->name();
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
        return screen->name();
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount,
                           modes.data(), nullptr) != ERROR_SUCCESS)
        return screen->name();
    const QSize size = screen->size() * screen->devicePixelRatio();
    QString best;
    qint64 bestDistance = -1;
    for (UINT32 i = 0; i < pathCount; ++i)
    {
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target),
                         paths[i].targetInfo.adapterId, paths[i].targetInfo.id};
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source),
                         paths[i].sourceInfo.adapterId, paths[i].sourceInfo.id};
        if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS ||
            DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
            paths[i].sourceInfo.modeInfoIdx >= modeCount ||
            QString::fromWCharArray(target.monitorFriendlyDeviceName)
                .compare(screen->name(), Qt::CaseInsensitive))
            continue;
        const auto& mode = modes[paths[i].sourceInfo.modeInfoIdx].sourceMode;
        if (QSize(int(mode.width), int(mode.height)) != size)
            continue;
        const qint64 distance = qAbs(qint64(mode.position.x) - screen->geometry().x()) +
                                qAbs(qint64(mode.position.y) - screen->geometry().y());
        if (bestDistance < 0 || distance < bestDistance)
        {
            bestDistance = distance;
            best = QString::fromWCharArray(source.viewGdiDeviceName);
        }
    }
    return best.isEmpty() ? screen->name() : best;
}
} // namespace

void CaptureSource::startNative(const QString& device, NativeCapture::Crop crop)
{
    const auto generation = nativeGeneration_;
    native_.start(
        device, [this](QImage image) { publish(std::move(image)); },
        [this, generation](QString error) {
            QMetaObject::invokeMethod(
                this,
                [this, error, generation] {
                    if (generation != nativeGeneration_)
                        return;
                    native_.stop();
                    emit failed(error);
                },
                Qt::QueuedConnection);
        },
        std::move(crop));
}
void CaptureSource::startNativeCapture(QScreen* screen) { startNative(windowsDisplayName(screen)); }
namespace {
bool cloaked(HWND window)
{
    DWORD value = 0;
    return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &value, sizeof(value))) && value;
}
bool windowBounds(HWND window, RECT& bounds)
{
    return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds,
                                           sizeof(bounds))) ||
           GetWindowRect(window, &bounds);
}
struct WindowList {
    QList<quintptr> handles;
    QVariantList labels;
};
BOOL CALLBACK collectWindow(HWND window, LPARAM parameter)
{
    auto& list = *reinterpret_cast<WindowList*>(parameter);
    if (!IsWindowVisible(window) || IsIconic(window) || cloaked(window) ||
        GetWindow(window, GW_OWNER) || (GetWindowLongW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW))
        return TRUE;
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId())
        return TRUE;
    wchar_t title[256];
    const int length = GetWindowTextW(window, title, 256);
    RECT bounds{};
    if (length <= 0 || !windowBounds(window, bounds) || bounds.right - bounds.left < 64 ||
        bounds.bottom - bounds.top < 64)
        return TRUE;
    list.handles.append(reinterpret_cast<quintptr>(window));
    list.labels.append(QVariantMap{{"label", QString::fromWCharArray(title, length)}});
    return TRUE;
}
} // namespace
bool CaptureSource::startWindowCapture(quintptr handle)
{
    const auto window = reinterpret_cast<HWND>(handle);
    if (!IsWindow(window))
    {
        emit failed("That window has closed; refresh the list.");
        return false;
    }
    MONITORINFOEXW monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
        return false;
    const QPoint origin(monitor.rcMonitor.left, monitor.rcMonitor.top);
    startNative(
        QString::fromWCharArray(monitor.szDevice),
        [window, origin](QRect& region) {
            RECT bounds{};
            if (!IsWindow(window) || IsIconic(window) || !windowBounds(window, bounds))
                return false;
            region = QRect(bounds.left - origin.x(), bounds.top - origin.y(),
                           bounds.right - bounds.left, bounds.bottom - bounds.top);
            return true;
        });
    return true;
}
QVariantList CaptureSource::refresh(int mode)
{
    QVariantList result;
    screens_.clear();
    windows_.clear();
    if (mode == Scene::DisplayCapture)
    {
        for (auto* screen : QGuiApplication::screens())
        {
            screens_.append(screen);
            result.append(QVariantMap{{"label", QString("%1 · %2 × %3")
                                                    .arg(screen->name())
                                                    .arg(screen->size().width())
                                                    .arg(screen->size().height())}});
        }
    } else if (mode == Scene::WindowCapture)
    {
        WindowList list;
        EnumWindows(collectWindow, reinterpret_cast<LPARAM>(&list));
        windows_ = list.handles;
        result = list.labels;
    }
    return result;
}
CaptureSource::~CaptureSource() { stop(); }
void CaptureSource::stop()
{
    ++nativeGeneration_;
    native_.stop();
    QMutexLocker lock(&mutex_);
    latest_ = {};
    ++serial_;
    publishedUs_ = 0;
}
bool CaptureSource::select(int mode, int index)
{
    stop();
    if (mode == Scene::DisplayCapture && index >= 0 && index < screens_.size() && screens_[index])
    {
        startNativeCapture(screens_[index].data());
        return true;
    }
    if (mode == Scene::WindowCapture && index >= 0 && index < windows_.size())
        return startWindowCapture(windows_[index]);
    return false;
}
bool CaptureSource::hasFrame() const
{
    QMutexLocker lock(&mutex_);
    return !latest_.isNull();
}
CaptureSource::Snapshot CaptureSource::snapshot() const
{
    QMutexLocker lock(&mutex_);
    return {latest_, serial_, publishedUs_};
}
QImage CaptureSource::fit(const QImage& image)
{
    if (image.isNull())
        return {};
    if (image.size() == QSize(profile::WIDTH, profile::HEIGHT) &&
        image.format() == QImage::Format_RGB32)
        return image;
    QImage output(profile::WIDTH, profile::HEIGHT, QImage::Format_RGB32);
    output.fill(Qt::black);
    const QSize size = image.size().scaled(output.size(), Qt::KeepAspectRatio);
    QPainter painter(&output);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(
        QRect(QPoint((profile::WIDTH - size.width()) / 2, (profile::HEIGHT - size.height()) / 2),
              size),
        image);
    return output;
}
} // namespace video
