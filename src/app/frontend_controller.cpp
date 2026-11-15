#include "app/frontend_controller.h"
#include "transport/device_transport.h"

FrontendController::FrontendController(QObject* parent) : QObject(parent) { refreshPorts(); }

void FrontendController::refreshPorts()
{
    auto available = QSerialPortInfo::availablePorts();
    std::sort(available.begin(), available.end(), [](const auto& a, const auto& b) {
        return a.portName().compare(b.portName(), Qt::CaseInsensitive) < 0;
    });
    QVariantList ports;
    int best = -1, bestScore = 0;
    for (const auto& port : available)
    {
        const auto description = port.description();
        const int score = bridgePortScore(port);
        if (score > bestScore)
        {
            bestScore = score;
            best = ports.size();
        }
        ports.append(QVariantMap{{"name", port.portName()},
                                 {"label", description.isEmpty()
                                               ? port.portName()
                                               : port.portName() + "  ·  " + description}});
    }
    if (ports == ports_ && best == suggested_)
        return;
    ports_ = ports;
    suggested_ = best;
    emit portsChanged();
}
