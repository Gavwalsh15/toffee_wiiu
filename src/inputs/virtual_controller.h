#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <memory>

class VirtualController : public QObject {
    Q_OBJECT
  public:
    enum class State { Off, Ready, DriverMissing, Installing, Failed };
    explicit VirtualController(QObject* parent = nullptr);
    ~VirtualController() override;
    bool enabled() const { return enabled_; }
    void setEnabled(bool value);
    bool xboxLayout() const { return xboxLayout_; }
    void setXboxLayout(bool value);
    State state() const { return state_; }
    QString status() const;
    void retry();
    void installDriver();
    void update(quint32 buttons, const QVariantList& axes);
    void release();
  signals:
    void stateChanged();
    void rumble(int strength);

  private:
    struct Impl;
    void connectBus();
    void disconnectBus();
    void setState(State state, int error = 0);
    std::unique_ptr<Impl> impl_;
    State state_ = State::Off;
    int error_ = 0;
    QTimer installWatch_;
    int installChecks_ = 0;
    bool enabled_ = false, xboxLayout_ = false;
};
