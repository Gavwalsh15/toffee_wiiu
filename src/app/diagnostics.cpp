#include "app/diagnostics.h"
#include "logger/toffee_log.h"
#include "stream/stream_controller.h"
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTextStream>
#include <QTimer>

namespace app {
int runWithDiagnostics(QGuiApplication& application, QQmlApplicationEngine& engine)
{
    QTimer captureTimer;
    int captureIndex = 0;
    const auto arguments = application.arguments();
    const int streamCheck = arguments.indexOf("--stream-check");
    const int durationArgument = arguments.indexOf("--check-seconds");
    const int readyArgument = arguments.indexOf("--ready-seconds");
    const int readySeconds = readyArgument >= 0 && readyArgument + 1 < arguments.size()
                                 ? qBound(1, arguments.at(readyArgument + 1).toInt(), 600)
                                 : 45;
    const int checkSeconds = durationArgument >= 0 && durationArgument + 1 < arguments.size()
                                 ? qBound(1, arguments.at(durationArgument + 1).toInt(), 300)
                                 : 30;
    QTimer checkTimer;
    bool checkStarted = false;
    bool startupSucceeded = false;
    int checkCycles = 0;
    if (streamCheck >= 0 && streamCheck + 1 < arguments.size())
    {
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        auto* session = window->findChild<StreamController*>("streamController");
        auto* page = window->findChild<QObject*>("connectPage");
        if (!session || !page)
            return 3;
        window->setProperty("currentPage", 1);
        page->setProperty("hasSource", true);
        if (arguments.contains("--capture-display"))
            session->setSceneMode(video::Scene::DisplayCapture);
        if (arguments.contains("--capture-window"))
            session->setSceneMode(video::Scene::WindowCapture);
        
        const auto targets = session->captureTargets();
        for (const auto& target : targets)
            QTextStream(stdout) << "CAPTURE TARGET " << target.toMap().value("label").toString()
                                << Qt::endl;
        const int targetArgument = arguments.indexOf("--capture-target");
        if (targetArgument >= 0 && targetArgument + 1 < arguments.size())
        {
            const auto wanted = arguments.at(targetArgument + 1);
            int found = -1;
            for (int i = 0; i < targets.size() && found < 0; ++i)
                if (targets[i].toMap().value("label").toString().contains(wanted,
                                                                          Qt::CaseInsensitive))
                    found = i;
            if (found < 0)
            {
                QTextStream(stdout) << "CAPTURE TARGET NOT FOUND " << wanted << Qt::endl;
                return 4;
            }
            session->setCaptureTarget(found);
            QTextStream(stdout) << "CAPTURING " << targets[found].toMap().value("label").toString()
                                << Qt::endl;
        }
        session->connectDevice(arguments.at(streamCheck + 1));
        if (arguments.contains("--reset"))
            QTimer::singleShot(300, session, &StreamController::restartDevice);
        checkTimer.setInterval(1000);
        QObject::connect(&checkTimer, &QTimer::timeout, &application, [&, session] {
            QTextStream(stdout) << session->status() << " | " << session->metrics() << Qt::endl;
            if (!session->error().isEmpty())
            {
                application.exit(1);
                return;
            }
            if (!session->captureError().isEmpty())
            {
                QTextStream(stdout) << "CAPTURE ERROR " << session->captureError() << Qt::endl;
                application.exit(1);
                return;
            }
            if (session->ready() && session->sourceReady() && !checkStarted)
            {
                if (arguments.contains("--freeze-capture"))
                    session->setPaused(true);
                checkStarted = startupSucceeded = true;
                session->startStream();
                if (arguments.contains("--recovery-phases"))
                {
                    QTextStream(stdout)
                        << "RECOVERY_PHASE at_us=" << pacing::nowUs() << " frozen=0" << Qt::endl;
                    for (int phase = 1; phase < 3; ++phase)
                        QTimer::singleShot(
                            phase * checkSeconds * 1000 / 3, &application, [session, phase] {
                                session->setPaused(phase == 1);
                                QTextStream(stdout) << "RECOVERY_PHASE at_us=" << pacing::nowUs()
                                                    << " frozen=" << session->paused() << Qt::endl;
                            });
                }
                if (arguments.contains("--preview-check"))
                {
                    auto* preview = window->findChild<QObject*>("scenePreview");
                    if (!preview)
                    {
                        application.exit(3);
                        return;
                    }
                    QTextStream(stdout) << "PACING_PHASE at_us=" << pacing::nowUs()
                                        << " live_preview=1" << Qt::endl;
                    for (int phase = 1; phase < 4; ++phase)
                        QTimer::singleShot(phase * 30000, &application, [preview, phase] {
                            const bool live = (phase % 2) == 0;
                            preview->setProperty("live", live);
                            QTextStream(stdout) << "PACING_PHASE at_us=" << pacing::nowUs()
                                                << " live_preview=" << live << Qt::endl;
                        });
                }
                QTimer::singleShot(checkSeconds * 1000, &application, [&, session] {
                    QTextStream(stdout) << "FINAL " << session->summary() << "\nLOG "
                                        << session->logPath() << Qt::endl;
                    const bool running = session->streaming();
                    if (running && arguments.contains("--lifecycle-check") && ++checkCycles < 3)
                    {
                        session->stopStream();
                        checkStarted = false;
                        QTextStream(stdout)
                            << "LIFECYCLE_RESTART completed=" << checkCycles << Qt::endl;
                        return;
                    }
                    QObject::connect(
                        session, &StreamController::deviceDisconnected, &application,
                        [&application, session, running] {
                            application.exit(running && session->error().isEmpty() ? 0 : 1);
                        },
                        Qt::SingleShotConnection);
                    session->disconnectDevice();
                });
            }
        });
        checkTimer.start();
        QTimer::singleShot(readySeconds * 1000, &application, [&, session] {
            if (startupSucceeded)
                return;
            QTextStream(stdout) << "READY TIMEOUT device_ready=" << session->ready()
                                << " source_ready=" << session->sourceReady()
                                << " capture_targets=" << session->captureTargets().size()
                                << Qt::endl;
            application.exit(2);
        });
    }
    const int captureArgument = arguments.indexOf("--capture-ui");
    if (arguments.contains("--smoke-test") && captureArgument >= 0 &&
        captureArgument + 1 < arguments.size())
    {
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        const QDir directory(arguments.at(captureArgument + 1));
        if (!window || !directory.mkpath("."))
            return 2;
        window->setProperty("currentPage", 0);
        captureTimer.setInterval(450);
        QObject::connect(&captureTimer, &QTimer::timeout, &application, [&, window, directory] {
            const QString filename = QString("page-%1.png").arg(captureIndex);
            const auto image = window->grabWindow();
            if (image.isNull() || !image.save(directory.filePath(filename)))
            {
                logger::error("Could not capture the frontend window");
                application.exit(2);
                return;
            }
            ++captureIndex;
            if (captureIndex == 9)
            {
                application.quit();
                return;
            }
            if (captureIndex >= 4)
                window->resize(900, 620);
            if (captureIndex == 8)
            {
                window->resize(1160, 800);
                if (auto* preview = window->findChild<QObject*>("connectPage"))
                    preview->setProperty("hasSource", true);
                if (arguments.contains("--capture-display"))
                    if (auto* session = window->findChild<StreamController*>("streamController"))
                        session->setSceneMode(video::Scene::DisplayCapture);
                window->setProperty("currentPage", 1);
            } else
            {
                window->setProperty("currentPage", captureIndex % 4);
            }
        });
        captureTimer.start();
    } else if (arguments.contains("--smoke-test"))
    {
        QTimer::singleShot(750, &application, &QCoreApplication::quit);
    }

    return application.exec();
}
} // namespace app
