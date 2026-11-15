#pragma once
#include <QObject>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class VirtualDisplay : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool installed READ installed NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool installing READ installing NOTIFY changed)
    // 0: 854x480, a 1:1 picture on the GamePad. 1: 1280x720. 2: 1920x1080.
    Q_PROPERTY(int size READ size WRITE setSize NOTIFY changed)
    Q_PROPERTY(bool needsUpdate READ needsUpdate NOTIFY changed)
    Q_PROPERTY(bool problem READ problem NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    // Such as \\.\DISPLAY5; matches QScreen::name().
    Q_PROPERTY(QString screenName READ screenName NOTIFY changed)
    Q_PROPERTY(QRect area READ area NOTIFY changed)
  public:
    explicit VirtualDisplay(QObject* parent = nullptr);
    ~VirtualDisplay() override;
    bool enabled() const { return enabled_; }
    void setEnabled(bool value);
    bool installed() const { return !adapter_.isEmpty(); }
    bool active() const { return active_; }
    bool installing() const { return installing_; }
    int size() const { return size_; }
    void setSize(int value);
    bool needsUpdate() const { return outdated_ || duplicates_; }
    bool problem() const { return !installed() || !error_.isEmpty() || needsUpdate(); }
    QString status() const;
    QString screenName() const { return active_ ? adapter_ : QString(); }
    QRect area() const { return active_ ? area_ : QRect(); }
    Q_INVOKABLE void installDriver();
    Q_INVOKABLE void refresh();
  signals:
    void changed();

  private:
    void detect();
    void attach();
    void detach();
    void watchInstaller();
    void release();
    bool owned() const;
    static void setOwned(bool value);
    QString adapter_; // \\.\DISPLAYn of the virtual adapter, empty when the driver is missing
    QString error_;
    QRect area_;
    QTimer installWatch_;
    void* installerProcess_ = nullptr;
    int size_ = 1;
    bool enabled_ = false, active_ = false, installing_ = false, outdated_ = false,
         duplicates_ = false;
};
