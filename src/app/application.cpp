#include "app/application.h"
#include "app/config.h"
#include "logger/toffee_log.h"
#if TOFFEE_APP_DIAGNOSTICS
#include "app/diagnostics.h"
#endif

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QSurfaceFormat>
#include "display/driver_installer.h"
#include <windows.h>
#include <shellapi.h>

namespace app {
int run(int argc, char** argv)
{
    int wideCount = 0;
    if (LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &wideCount))
    {
        const bool install = wideCount == 3 && wcscmp(wide[1], L"--install-display-driver") == 0;
        const int result = install ? display::installDriver(wide[2]) : -1;
        LocalFree(wide);
        if (install)
            return result;
    }
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication application(argc, argv);
    QGuiApplication::setApplicationName(config::APP_NAME);
    QGuiApplication::setApplicationVersion(config::APP_VERSION);
    QGuiApplication::setOrganizationName(config::APP_NAME);
    QQuickStyle::setStyle("Basic");
    logger::info(QByteArray("Toffee ").append(config::APP_VERSION).append(" starting").constData());

    QQmlApplicationEngine engine;
    engine.loadFromModule("Toffee", "Main");
    if (engine.rootObjects().isEmpty())
    {
        logger::error("Could not load the Toffee QML window");
        return 1;
    }

#if TOFFEE_APP_DIAGNOSTICS
    return runWithDiagnostics(application, engine);
#else
    return application.exec();
#endif
}
} // namespace app
