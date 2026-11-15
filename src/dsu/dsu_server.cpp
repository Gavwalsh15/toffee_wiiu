#include "dsu_server.h"
#include "common/crc32.h"
#include "inputs/gamepad_buttons.h"
#include "inputs/input_controller.h"
#include <QRandomGenerator>
#include <QtEndian>
#include <algorithm>
#include <cstring>

namespace {
constexpr quint16 ProtocolVersion = 1001;
constexpr quint32 TypeVersion = 0x100000, TypePorts = 0x100001, TypePad = 0x100002;
constexpr int HeaderSize = 16;
constexpr qint64 ClientTimeoutMs = 5000;
constexpr qint64 MinSendIntervalUs = 2000;

using namespace gamepad;

quint32 crc32(const QByteArray& data) { return toffee_crc32(data.constData(), size_t(data.size())); }
struct Writer {
    QByteArray bytes;
    void u8(quint8 v) { bytes.append(char(v)); }
    void u16(quint16 v)
    {
        u8(quint8(v));
        u8(quint8(v >> 8));
    }
    void u32(quint32 v)
    {
        u16(quint16(v));
        u16(quint16(v >> 16));
    }
    void u64(quint64 v)
    {
        u32(quint32(v));
        u32(quint32(v >> 32));
    }
    void f32(float v)
    {
        quint32 bits;
        std::memcpy(&bits, &v, sizeof(bits));
        u32(bits);
    }
};

quint32 read32(const QByteArray& data, int offset)
{
    return qFromLittleEndian<quint32>(data.constData() + offset);
}

quint8 stickByte(const QVariantList& axes, int index)
{
    const double value = index < axes.size() ? axes[index].toDouble() : 0.0;
    return quint8(std::lround(128.0 + std::clamp(value, -1.0, 1.0) * 127.0));
}
} // namespace

DsuServer::DsuServer(QObject* parent) : QObject(parent)
{
    clock_.start();
    id_ = QRandomGenerator::global()->generate();
    connect(&socket_, &QUdpSocket::readyRead, this, &DsuServer::receive);
}

void DsuServer::setEnabled(bool value)
{
    if (value == enabled_)
        return;
    enabled_ = value;
    listen();
}

void DsuServer::setPort(quint16 port)
{
    if (port == port_)
        return;
    port_ = port;
    listen();
}

void DsuServer::setInput(InputController* input)
{
    if (input == input_)
        return;
    if (input_)
        disconnect(input_, nullptr, this, nullptr);
    input_ = input;
    if (input_)
        connect(input_, &InputController::changed, this, &DsuServer::sendPad);
    emit changed();
}

void DsuServer::listen()
{
    socket_.close();
    clients_.clear();
    error_.clear();
    if (enabled_ && !socket_.bind(QHostAddress::LocalHost, port_, QAbstractSocket::DontShareAddress))
        error_ = socket_.errorString();
    emit changed();
}

QString DsuServer::status() const
{
    if (!enabled_)
        return "Off";
    if (!error_.isEmpty())
        return QString("Could not listen on port %1: %2").arg(port_).arg(error_);
    return QString("Listening on 127.0.0.1:%1, %2 program%3 connected")
        .arg(port_)
        .arg(clients_.size())
        .arg(clients_.size() == 1 ? "" : "s");
}

void DsuServer::receive()
{
    while (socket_.hasPendingDatagrams())
    {
        QByteArray packet(int(socket_.pendingDatagramSize()), Qt::Uninitialized);
        QHostAddress from;
        quint16 fromPort = 0;
        const auto size = socket_.readDatagram(packet.data(), packet.size(), &from, &fromPort);
        if (size < 0)
            continue;
        packet.resize(int(size));
        handle(packet, from, fromPort);
    }
}

void DsuServer::handle(const QByteArray& packet, const QHostAddress& from, quint16 port)
{
    if (packet.size() < HeaderSize + 4 || !packet.startsWith("DSUC") ||
        qFromLittleEndian<quint16>(packet.constData() + 6) != packet.size() - HeaderSize)
        return;
    QByteArray check = packet;
    std::memset(check.data() + 8, 0, 4);
    if (crc32(check) != read32(packet, 8))
        return;
    const quint32 type = read32(packet, HeaderSize);
    if (type == TypeVersion)
    {
        Writer w;
        w.u16(ProtocolVersion);
        socket_.writeDatagram(frame(TypeVersion, w.bytes), from, port);
    } else if (type == TypePorts && packet.size() >= HeaderSize + 8)
    {
        const int count = std::clamp(int(qFromLittleEndian<qint32>(packet.constData() + 20)), 0, 4);
        for (int i = 0; i < count && HeaderSize + 8 + i < packet.size(); ++i)
        {
            const quint8 slot = quint8(packet[HeaderSize + 8 + i]);
            QByteArray info = slot == 0 ? slotInfo() : QByteArray(11, '\0');
            if (slot != 0)
                info[0] = char(slot);
            info.append('\0');
            socket_.writeDatagram(frame(TypePorts, info), from, port);
        }
    } else if (type == TypePad)
    {
        // Every request is a (re)subscription for slot 0
        const qint64 now = clock_.elapsed();
        expire();
        const auto found = std::find_if(clients_.begin(), clients_.end(), [&](const Client& c) {
            return c.address == from && c.port == port;
        });
        if (found != clients_.end())
            found->lastSeen = now;
        else
        {
            clients_.append({from, port, now});
            emit changed();
        }
    }
}

void DsuServer::expire()
{
    const qint64 now = clock_.elapsed();
    const auto before = clients_.size();
    clients_.removeIf([&](const Client& c) { return now - c.lastSeen > ClientTimeoutMs; });
    if (clients_.size() != before)
        emit changed();
}

QByteArray DsuServer::frame(quint32 type, const QByteArray& payload) const
{
    Writer w;
    w.bytes.append("DSUS", 4);
    w.u16(ProtocolVersion);
    w.u16(quint16(payload.size() + 4));
    w.u32(0); // CRC32
    w.u32(id_);
    w.u32(type);
    w.bytes.append(payload);
    const quint32 crc = crc32(w.bytes);
    for (int i = 0; i < 4; ++i)
        w.bytes[8 + i] = char(crc >> (8 * i));
    return w.bytes;
}

QByteArray DsuServer::slotInfo() const
{
    const bool connected = input_ && input_->active();
    Writer w;
    w.u8(0);                 // slot
    w.u8(connected ? 2 : 0); // 0 disconnected, 2 connected
    w.u8(connected ? 2 : 0); // full gyro
    w.u8(connected ? 2 : 0); // connection type
    const quint8 mac[6] = {0x54, 0x4F, 0x46, 0x46, 0x45, 0x45}; // "TOFFEE"
    for (const auto b : mac)
        w.u8(connected ? b : 0);
    w.u8(connected ? 5 : 0); // battery: full
    return w.bytes;
}

QByteArray DsuServer::padData()
{
    const quint32 buttons = input_->buttons();
    auto has = [&](quint32 mask) { return (buttons & mask) != 0; };
    const auto axes = input_->axes();
    Writer w;
    w.bytes = slotInfo();
    w.u8(1); // active
    w.u32(packetNumber_++);
    // The face buttons follow the physical layout, as the Xbox button positions option does: the
    // GamePad's right button is Circle, bottom is Cross, top is Triangle and left is Square.
    w.u8(quint8(has(Minus) << 0 | has(L3) << 1 | has(R3) << 2 | has(Plus) << 3 | has(Up) << 4 |
                has(Right) << 5 | has(Down) << 6 | has(Left) << 7));
    w.u8(quint8(has(PadX) << 0 | has(PadA) << 1 | has(PadB) << 2 | has(PadY) << 3 | has(R) << 4 |
                has(L) << 5 | has(ZR) << 6 | has(ZL) << 7));
    w.u8(has(Home));
    w.u8(0); // touch pad button
    for (int stick = 0; stick < 4; ++stick)
        w.u8(stickByte(axes, stick));
    // Analogue buttons: dpad left, down, right, up, then Square, Cross, Circle, Triangle, R1, L1,
    // R2, L2.
    for (const auto mask : {Left, Down, Right, Up, PadY, PadB, PadA, PadX, R, L, ZR, ZL})
        w.u8(has(mask) ? 255 : 0);
    const bool touching = input_->touchActive();
    if (touching && !wasTouching_)
        touchId_ = (touchId_ + 1) & 0xFF;
    wasTouching_ = touching;
    w.u8(touching);
    w.u8(quint8(touchId_));
    w.u16(quint16(std::clamp(input_->touchX(), 0, 4095) * 1919 / 4095));
    w.u16(quint16(std::clamp(input_->touchY(), 0, 4095) * 941 / 4095));
    for (int i = 0; i < 6; ++i)
        w.u8(0); // second touch: unused
    w.u64(quint64(clock_.nsecsElapsed() / 1000));
    for (const auto& value : input_->accel())
        w.f32(float(value.toDouble()));
    for (const auto& value : input_->gyro())
        w.f32(float(value.toDouble()));
    return w.bytes;
}

void DsuServer::sendPad()
{
    const bool active = input_ && input_->active();
    // A pad that has gone quiet gets one last packet, so a held button is not held forever.
    const bool released = !active && sentActive_;
    if (clients_.isEmpty() || !input_ || (!active && !released))
        return;
    const qint64 nowUs = clock_.nsecsElapsed() / 1000;
    if (!released && lastSent_ >= 0 && nowUs - lastSent_ < MinSendIntervalUs)
        return;
    lastSent_ = nowUs;
    sentActive_ = active;
    expire();
    if (clients_.isEmpty())
        return;
    const QByteArray packet = frame(TypePad, padData());
    for (const auto& client : clients_)
        socket_.writeDatagram(packet, client.address, client.port);
}
