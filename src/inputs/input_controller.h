#pragma once
#include <QObject>
#include <QRect>
#include <QVariantList>
#include <QElapsedTimer>
#include <QTimer>
#include "gamepad_buttons.h"
#include "virtual_controller.h"
class InputController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(quint32 buttons READ buttons NOTIFY changed)
    Q_PROPERTY(QVariantList controls READ controls CONSTANT)
    Q_PROPERTY(QVariantList axes READ axes NOTIFY changed)
    Q_PROPERTY(QVariantList rawAxes READ rawAxes NOTIFY changed)
    Q_PROPERTY(QVariantList accel READ accel NOTIFY changed)
    Q_PROPERTY(QVariantList gyro READ gyro NOTIFY changed)
    Q_PROPERTY(bool touchActive READ touchActive NOTIFY changed)
    Q_PROPERTY(int touchX READ touchX NOTIFY changed)
    Q_PROPERTY(int touchY READ touchY NOTIFY changed)
    Q_PROPERTY(bool touchAsMouse READ touchAsMouse WRITE setTouchAsMouse NOTIFY changed)
    Q_PROPERTY(bool realTouch READ realTouch WRITE setRealTouch NOTIFY changed)
    Q_PROPERTY(QRect touchArea READ touchArea WRITE setTouchArea NOTIFY changed)
    Q_PROPERTY(quint64 reports READ reports NOTIFY changed)
    Q_PROPERTY(double deadZone READ deadZone WRITE setDeadZone NOTIFY changed)
    Q_PROPERTY(bool virtualPad READ virtualPad WRITE setVirtualPad NOTIFY changed)
    Q_PROPERTY(bool xboxLayout READ xboxLayout WRITE setXboxLayout NOTIFY changed)
    Q_PROPERTY(QString virtualPadStatus READ virtualPadStatus NOTIFY changed)
    Q_PROPERTY(bool virtualPadDriverMissing READ virtualPadDriverMissing NOTIFY changed)
  public:
    explicit InputController(QObject* parent = nullptr);
    ~InputController() override;
    bool active() const { return active_; }
    quint32 buttons() const { return buttons_; }
    QVariantList axes() const;
    QVariantList rawAxes() const;
    QVariantList controls() const;
    QVariantList accel() const
    {
        using gamepad::AccelPerG;
        return {accel_[0] / AccelPerG, accel_[1] / AccelPerG, accel_[2] / AccelPerG};
    }
    QVariantList gyro() const
    {
        using gamepad::GyroDegreesPerCount;
        return {gyro_[0] * GyroDegreesPerCount, gyro_[1] * GyroDegreesPerCount,
                gyro_[2] * GyroDegreesPerCount};
    }
    bool touchActive() const { return touchActive_; }
    int touchX() const { return touchX_; }
    int touchY() const { return touchY_; }
    bool touchAsMouse() const { return touchAsMouse_; }
    quint64 reports() const { return reports_; }
    double deadZone() const { return deadZone_; }
    void setDeadZone(double value);
    void setTouchAsMouse(bool value);
    bool realTouch() const { return realTouch_; }
    void setRealTouch(bool value);
    QRect touchArea() const { return touchArea_; }
    void setTouchArea(const QRect& value);
    bool virtualPad() const { return virtualPad_.enabled(); }
    void setVirtualPad(bool value) { virtualPad_.setEnabled(value); }
    bool xboxLayout() const { return virtualPad_.xboxLayout(); }
    void setXboxLayout(bool value) { virtualPad_.setXboxLayout(value); }
    QString virtualPadStatus() const { return virtualPad_.status(); }
    bool virtualPadDriverMissing() const
    {
        return virtualPad_.state() == VirtualController::State::DriverMissing;
    }
    Q_INVOKABLE void retryVirtualPad() { virtualPad_.retry(); }
    Q_INVOKABLE void installVirtualPadDriver() { virtualPad_.installDriver(); }
    bool report(quint32 sequence, const QByteArray& report);
    // A report as the bridge sends it, from the decoded values (for `toffee-cli dsu --simulate`).
    static QByteArray makeReport(quint32 buttons, const quint16 (&sticks)[4]);
    void bridgeLost();
    void clear();
    void reset();
  signals:
    void changed();
    void rumbleRequested(int strength);

  private:
    void expire();
    void updateMouse(bool touchActive);
    bool active_ = false, haveSequence_ = false;
    bool touchActive_ = false;
    bool touchAsMouse_ = false, mouseButtonDown_ = false;
    bool realTouch_ = true, pressedAsTouch_ = false;
    QRect touchArea_;
    int lastX_ = 0, lastY_ = 0;
    quint32 buttons_ = 0, sequence_ = 0;
    quint16 raw_[4] = {gamepad::StickCentre, gamepad::StickCentre, gamepad::StickCentre,
                       gamepad::StickCentre};
    int touchX_ = 0, touchY_ = 0;
    int accel_[3] = {0, 0, 0}, gyro_[3] = {0, 0, 0};
    quint64 reports_ = 0;
    double deadZone_ = 0.1;
    QElapsedTimer lastReport_;
    QTimer expiry_;
    VirtualController virtualPad_;
};
