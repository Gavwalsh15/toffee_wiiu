#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <memory>

class QTemporaryDir;

class FirmwareFlasher : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool customBuildLoaded READ customBuildLoaded NOTIFY changed)
    Q_PROPERTY(bool flashing READ flashing NOTIFY changed)
    Q_PROPERTY(int progress READ progress NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString selectionError READ selectionError NOTIFY changed)
    Q_PROPERTY(QString firmwareName READ firmwareName NOTIFY changed)
    Q_PROPERTY(QString firmwareSize READ firmwareSize NOTIFY changed)
  public:
    explicit FirmwareFlasher(QObject* parent = nullptr);
    ~FirmwareFlasher() override;
    bool customBuildLoaded() const { return customBuildLoaded_; }
    bool flashing() const { return process_.state() != QProcess::NotRunning; }
    int progress() const { return progress_; }
    QString status() const { return status_; }
    QString error() const { return error_; }
    QString selectionError() const { return selectionError_; }
    QString firmwareSize() const { return firmwareSize_; }
    QString firmwareName() const
    {
        return customBuildLoaded_ ? customBuildName_ : "Toffee for ESP32-C5";
    }

    Q_INVOKABLE void flash(const QString& port);
    Q_INVOKABLE bool loadCustomBuild(const QUrl& manifestUrl);
    Q_INVOKABLE void useBundledFirmware();

  signals:
    void changed();
    void finished(bool success);

  private:
    void consumeOutput(const QByteArray& output);
    void setError(const QString& message);
    void finish(int exitCode, QProcess::ExitStatus exitStatus);

    QProcess process_;
    std::unique_ptr<QTemporaryDir> temporary_;
    QJsonObject manifest_;
    QJsonArray images_;
    QJsonObject releaseManifest_;
    QJsonArray releaseImages_;
    QString firmwareSize_;
    QString customBuildName_;
    QString outputTail_;
    QString status_ = "Ready to flash";
    QString error_;
    QString selectionError_;
    int progress_ = 0;
    bool customBuildLoaded_ = false;
};
