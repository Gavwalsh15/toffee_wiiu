#include "dsu/dsu_server.h"
#include "inputs/gamepad_buttons.h"
#include "inputs/input_controller.h"
#include "stream/stream_controller.h"
#include "app/config.h"
#include "transport/device_transport.h"
#include <QCommandLineParser>
#include <QDateTime>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>
#include <windows.h>

namespace {
enum ExitCode { Ok = 0, Failed = 1, Usage = 2, NoDevice = 3, NoSource = 4 };

const char* const Version = app::config::APP_VERSION;
constexpr double Pi = 3.14159265358979;

QTextStream& out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream& err()
{
    static QTextStream stream(stderr);
    return stream;
}

BOOL WINAPI onInterrupt(DWORD)
{
    QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection);
    return TRUE;
}

QString findBridge()
{
    QString best;
    int bestScore = 0;
    for (const auto& port : QSerialPortInfo::availablePorts())
        if (const int score = bridgePortScore(port); score > bestScore)
        {
            best = port.portName();
            bestScore = score;
        }
    return best;
}

void usage()
{
    out() << "toffee-cli " << Version << R"(: stream to the Wii U GamePad and read its input

Usage: toffee-cli <command> [options]

Commands:
  ports      List serial ports and mark the likely bridge
  sources    List the displays and windows that can be streamed
  stream     Stream a display or window to the GamePad until stopped (Ctrl+C)
  input      Print the GamePad's input as it arrives
  dsu        Serve GamePad input over DSU without streaming
  pair       Make the bridge forget its GamePad and wait for a new one to pair
  help       Show this text

Run "toffee-cli <command> --help" for a command's options.
Exit codes: 0 done, 1 failed, 2 bad usage, 3 bridge not found, 4 source not found.
)";
}

void emitEvent(bool json, const QString& event, const QString& text, const QJsonObject& more = {})
{
    if (!json)
    {
        out() << text << Qt::endl;
        return;
    }
    QJsonObject object = more;
    object["event"] = event;
    object["message"] = text;
    out() << QJsonDocument(object).toJson(QJsonDocument::Compact) << Qt::endl;
}

void reportPairing(const StreamController& stream, bool json, int& shown)
{
    const int code = stream.pairingCodeValue();
    if (code == shown)
        return;
    shown = code;
    if (code >= 0)
        emitEvent(json, "pairing",
                  "Pair the GamePad: press its SYNC button and enter " + stream.pairingCode() +
                      " (" + stream::pairingNames(code) + ")",
                  {{"code", stream::pairingNames(code)}});
}

int listPorts(bool json)
{
    QJsonArray array;
    const auto ports = QSerialPortInfo::availablePorts();
    for (const auto& port : ports)
    {
        const bool bridge = bridgePortScore(port) > 0;
        if (json)
            array.append(QJsonObject{{"name", port.portName()},
                                     {"description", port.description()},
                                     {"manufacturer", port.manufacturer()},
                                     {"likelyBridge", bridge}});
        else
            out() << port.portName().leftJustified(8) << port.description()
                  << (bridge ? "  <- likely bridge" : "") << Qt::endl;
    }
    if (json)
        out() << QJsonDocument(array).toJson(QJsonDocument::Compact) << Qt::endl;
    else if (ports.isEmpty())
        out() << "No serial ports found." << Qt::endl;
    return Ok;
}

int listSources(bool json)
{
    StreamController stream;
    QJsonArray array;
    for (const int mode : {video::Scene::DisplayCapture, video::Scene::WindowCapture})
    {
        stream.setSceneMode(mode);
        const auto targets = stream.captureTargets();
        for (int i = 0; i < targets.size(); ++i)
        {
            const QString label = targets[i].toMap().value("label").toString();
            const char* kind = mode == video::Scene::DisplayCapture ? "display" : "window";
            if (json)
                array.append(QJsonObject{{"kind", kind}, {"index", i}, {"label", label}});
            else
                out() << kind << ' ' << QString::number(i).rightJustified(3) << "  " << label
                      << Qt::endl;
        }
    }
    if (json)
        out() << QJsonDocument(array).toJson(QJsonDocument::Compact) << Qt::endl;
    return Ok;
}

bool selectSource(StreamController& stream, int mode, const QString& wanted)
{
    if (stream.sceneMode() != mode)
        stream.setSceneMode(mode);
    else
        stream.refreshCaptureTargets();
    const auto targets = stream.captureTargets();
    static const QRegularExpression digits("^\\d+$");
    if (digits.match(wanted).hasMatch())
    {
        const int index = wanted.toInt();
        if (index >= targets.size())
            return false;
        stream.setCaptureTarget(index);
        return true;
    }
    for (int i = 0; i < targets.size(); ++i)
    {
        const QString label = targets[i].toMap().value("label").toString();
        if (label.contains(wanted, Qt::CaseInsensitive))
        {
            stream.setCaptureTarget(i);
            return true;
        }
    }
    return false;
}

void addInputOptions(QCommandLineParser& parser)
{
    parser.addOption({"port", "Serial port of the bridge (default: find it)", "name"});
    parser.addOption({"json", "Print events as JSON lines"});
    parser.addOption({"dsu", "Serve input over DSU on 127.0.0.1 for any DSU client"});
    parser.addOption({"dsu-port", "DSU port (default 26760)", "port", "26760"});
}

int openBridge(StreamController& stream, const QCommandLineParser& parser, bool json)
{
    QString port = parser.value("port");
    if (port.isEmpty())
        port = findBridge();
    if (port.isEmpty())
    {
        err() << "No bridge found. Plug it in, or give --port. See: toffee-cli ports" << Qt::endl;
        return NoDevice;
    }
    emitEvent(json, "connecting", "Connecting to " + port, {{"port", port}});
    stream.connectDevice(port);
    return Ok;
}

int runSession(QGuiApplication& app, const QStringList& arguments, const QString& command)
{
    const bool streaming = command == "stream";
    QCommandLineParser parser;
    parser.addHelpOption();
    addInputOptions(parser);
    if (streaming)
    {
        parser.addOption({"display", "Display to stream: index or part of its name", "display"});
        parser.addOption({"window", "Window to stream: part of its title", "title"});
        parser.addOption({"no-audio", "Do not send PC audio"});
        parser.addOption({"audio-gain", "Audio volume boost, 0.5 to 4 (default 2)", "gain", "2"});
        parser.addOption({"touch-mouse", "Let the GamePad's touch screen move the mouse"});
        parser.addOption(
            {"virtual-pad", "Present the GamePad to Windows as an Xbox 360 controller"});
        parser.addOption({"xbox-layout", "With --virtual-pad: bottom button is A"});
        parser.addOption({"seconds", "Stop after this many seconds", "n"});
        parser.addOption(
            {"source-wait", "Seconds to wait for the window to appear (default 30)", "n", "30"});
    }
    parser.process(arguments);
    const bool json = parser.isSet("json");

    StreamController stream;
    DsuServer dsu;
    dsu.setInput(stream.input());
    if (parser.isSet("dsu"))
    {
        bool ok = false;
        const int port = parser.value("dsu-port").toInt(&ok);
        if (!ok || port < 1 || port > 65535)
        {
            err() << "--dsu-port must be 1 to 65535" << Qt::endl;
            return Usage;
        }
        dsu.setPort(quint16(port));
        dsu.setEnabled(true);
        emitEvent(json, "dsu", dsu.status());
        if (dsu.status().startsWith("Could not"))
            return Failed;
    }

    QString window, display;
    if (streaming)
    {
        window = parser.value("window");
        display = parser.value("display");
        if (!window.isEmpty() && !display.isEmpty())
        {
            err() << "Choose one of --display and --window" << Qt::endl;
            return Usage;
        }
        if (window.isEmpty() && display.isEmpty())
            display = "0";
        stream.setAudioEnabled(!parser.isSet("no-audio"));
        stream.setAudioGain(parser.value("audio-gain").toDouble());
        stream.input()->setTouchAsMouse(parser.isSet("touch-mouse"));
        stream.input()->setXboxLayout(parser.isSet("xbox-layout"));
        stream.input()->setVirtualPad(parser.isSet("virtual-pad"));
        if (!window.isEmpty())
            stream.setSceneMode(video::Scene::WindowCapture);
        else if (!selectSource(stream, video::Scene::DisplayCapture, display))
        {
            err() << "No display matches \"" << display << "\". See: toffee-cli sources"
                  << Qt::endl;
            return NoSource;
        }
    }

    if (const int code = openBridge(stream, parser, json); code != Ok)
        return code;

    SetConsoleCtrlHandler(onInterrupt, TRUE);
    int result = Ok;
    bool started = false, waitedForWindow = false, sawReady = false;
    int shownPairing = -1;
    QElapsedTimer sinceStart;
    sinceStart.start();
    QString lastStatus;

    QTimer tick;
    tick.setInterval(250);
    QObject::connect(&tick, &QTimer::timeout, &app, [&] {
        reportPairing(stream, json, shownPairing);
        if (stream.ready() && !sawReady)
        {
            sawReady = true;
            emitEvent(json, "ready", "GamePad ready");
        }
        if (!streaming)
            return;
        if (!window.isEmpty() && !started && stream.captureTarget() < 0)
        {
            if (!selectSource(stream, video::Scene::WindowCapture, window))
            {
                if (!waitedForWindow)
                    emitEvent(json, "waiting", "Waiting for a window matching \"" + window + "\"");
                waitedForWindow = true;
                if (sinceStart.elapsed() > parser.value("source-wait").toInt() * 1000)
                {
                    err() << "No window matches \"" << window << "\". See: toffee-cli sources"
                          << Qt::endl;
                    result = NoSource;
                    app.quit();
                }
                return;
            }
        }
        if (!started && stream.ready() && stream.sourceReady() && stream.captureError().isEmpty())
        {
            started = true;
            stream.startStream();
            emitEvent(json, "streaming", "Streaming");
        }
        if (!stream.captureError().isEmpty())
        {
            err() << stream.captureError() << Qt::endl;
            result = Failed;
            app.quit();
        }
    });
    tick.start();

    QTimer report;
    report.setInterval(1000);
    QObject::connect(&report, &QTimer::timeout, &app, [&] {
        const QString line = stream.status() + (stream.streaming() ? " | " + stream.metrics() : "");
        if (json)
            emitEvent(true, "status", line,
                      {{"streaming", stream.streaming()},
                       {"ready", stream.ready()},
                       {"dsuClients", dsu.clients()}});
        else if (line != lastStatus || stream.streaming())
            out() << line << Qt::endl;
        lastStatus = line;
        if (!stream.error().isEmpty())
            err() << stream.error() << Qt::endl;
    });
    report.start();

    if (command == "input")
    {
        QObject::connect(stream.input(), &InputController::changed, &app, [&] {
            auto* input = stream.input();
            static qint64 last = 0;
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - last < 100)
                return;
            last = now;
            const auto axes = input->axes();
            const QString line = QString("buttons=0x%1 axes=[%2 %3 %4 %5] touch=%6 (%7,%8)")
                                     .arg(input->buttons(), 6, 16, QChar('0'))
                                     .arg(axes.value(0).toDouble(), 0, 'f', 2)
                                     .arg(axes.value(1).toDouble(), 0, 'f', 2)
                                     .arg(axes.value(2).toDouble(), 0, 'f', 2)
                                     .arg(axes.value(3).toDouble(), 0, 'f', 2)
                                     .arg(input->touchActive())
                                     .arg(input->touchX())
                                     .arg(input->touchY());
            if (json)
                emitEvent(true, "input", line,
                          {{"buttons", qint64(input->buttons())},
                           {"axes", QJsonArray::fromVariantList(axes)},
                           {"accel", QJsonArray::fromVariantList(input->accel())},
                           {"gyro", QJsonArray::fromVariantList(input->gyro())},
                           {"touch", input->touchActive()},
                           {"touchX", input->touchX()},
                           {"touchY", input->touchY()}});
            else
                out() << line << Qt::endl;
        });
    }
    if (streaming && parser.isSet("seconds"))
        QTimer::singleShot(parser.value("seconds").toInt() * 1000, &app, &QCoreApplication::quit);

    const int code = app.exec();
    emitEvent(json, "stopped", "Stopped");
    return result != Ok ? result : code;
}

int runDsuDemo(QGuiApplication& app, const QStringList& arguments)
{
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"dsu-port", "DSU port (default 26760)", "port", "26760"});
    parser.addOption({"simulate", "Send moving fake input instead of using the bridge"});
    parser.process(arguments);
    InputController input;
    DsuServer dsu;
    dsu.setInput(&input);
    dsu.setPort(quint16(parser.value("dsu-port").toInt()));
    dsu.setEnabled(true);
    out() << dsu.status() << "\nSending fake input: the left stick circles and the buttons cycle."
          << Qt::endl;
    if (dsu.status().startsWith("Could not"))
        return Failed;
    SetConsoleCtrlHandler(onInterrupt, TRUE);
    quint32 sequence = 1;
    QElapsedTimer clock;
    clock.start();
    QTimer feed;
    feed.setInterval(5);
    QObject::connect(&feed, &QTimer::timeout, &app, [&] {
        const double t = clock.elapsed() / 1000.0;
        quint16 sticks[4];
        for (int i = 0; i < 4; ++i)
        {
            const double angle = t * 2.0 + (i % 2) * Pi / 2;
            sticks[i] = quint16(
                std::lround(gamepad::StickCentre + 1000 * (i < 2 ? std::cos(angle) : 0)));
        }
        input.report(sequence++,
                     InputController::makeReport(gamepad::PadA >> (int(t) % 4), sticks));
    });
    feed.start();
    QTimer status;
    status.setInterval(2000);
    QObject::connect(&status, &QTimer::timeout, &app, [&] { out() << dsu.status() << Qt::endl; });
    status.start();
    return app.exec();
}

int runPair(QGuiApplication& app, const QStringList& arguments)
{
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"port", "Serial port of the bridge (default: find it)", "name"});
    parser.addOption({"json", "Print events as JSON lines"});
    parser.process(arguments);
    const bool json = parser.isSet("json");

    StreamController stream;
    if (const int code = openBridge(stream, parser, json); code != Ok)
        return code;
    SetConsoleCtrlHandler(onInterrupt, TRUE);
    int result = Failed;
    int waitedMs = 0, shownPairing = -1;
    bool asked = false, sawCode = false;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &app, [&] {
        waitedMs += 50;
        if (!asked && stream.connected())
        {
            asked = true;
            stream.forgetPairing();
            emitEvent(json, "pair",
                      "The bridge is restarting into pairing (Ctrl+C to stop waiting)");
        } else if (!asked && waitedMs >= 5000)
        {
            err() << "Could not open the bridge's port" << Qt::endl;
            app.quit();
        }
        if (!asked)
            return;
        reportPairing(stream, json, shownPairing);
        sawCode = sawCode || shownPairing >= 0;
        if (sawCode && stream.ready())
        {
            emitEvent(json, "paired", "GamePad paired");
            result = Ok;
            app.quit();
        }
    });
    poll.start(50);
    app.exec();
    return result;
}
} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(app::config::APP_NAME);
    QGuiApplication::setApplicationVersion(app::config::APP_VERSION);
    QGuiApplication::setOrganizationName(app::config::APP_NAME);
    QStringList arguments = app.arguments();
    if (arguments.size() < 2 || arguments[1] == "help" || arguments[1] == "--help" ||
        arguments[1] == "-h")
    {
        usage();
        return arguments.size() < 2 ? Usage : Ok;
    }
    if (arguments[1] == "--version" || arguments[1] == "version")
    {
        out() << "toffee-cli " << Version << Qt::endl;
        return Ok;
    }
    const QString command = arguments[1];
    arguments.removeAt(1);
    const bool json = arguments.contains("--json");
    if (command == "ports")
        return listPorts(json);
    if (command == "sources")
        return listSources(json);
    if (command == "stream" || command == "input")
        return runSession(app, arguments, command);
    if (command == "pair")
        return runPair(app, arguments);
    if (command == "dsu")
    {
        if (arguments.contains("--simulate"))
            return runDsuDemo(app, arguments);
        arguments.append("--dsu");
        return runSession(app, arguments, "dsu");
    }
    err() << "Unknown command \"" << command << "\".\n" << Qt::endl;
    usage();
    return Usage;
}
