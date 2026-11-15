#pragma once
#include <QElapsedTimer>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QUdpSocket>
#include "inputs/input_controller.h"

class DsuServer : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(InputController* input READ input WRITE setInput NOTIFY changed)
    Q_PROPERTY(int clients READ clients NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
  public:
    static constexpr quint16 DefaultPort = 26760;
    explicit DsuServer(QObject* parent = nullptr);
    bool enabled() const { return enabled_; }
    void setEnabled(bool value);
    InputController* input() const { return input_; }
    void setInput(InputController* input);
    quint16 port() const { return port_; }
    void setPort(quint16 port);
    int clients() const { return clients_.size(); }
    QString status() const;

  signals:
    void changed();

  private:
    struct Client {
        QHostAddress address;
        quint16 port;
        qint64 lastSeen;
    };
    void listen();
    void receive();
    void handle(const QByteArray& packet, const QHostAddress& from, quint16 port);
    void sendPad();
    QByteArray frame(quint32 type, const QByteArray& payload) const;
    QByteArray slotInfo() const;
    QByteArray padData();
    void expire();

    QUdpSocket socket_;
    InputController* input_ = nullptr;
    QList<Client> clients_;
    QElapsedTimer clock_;
    qint64 lastSent_ = -1;
    bool sentActive_ = false; // the last pad packet showed an active pad
    quint32 id_ = 0, packetNumber_ = 0;
    quint16 port_ = DefaultPort;
    bool enabled_ = false;
    int touchId_ = 0;
    bool wasTouching_ = false;
    QString error_;
};
