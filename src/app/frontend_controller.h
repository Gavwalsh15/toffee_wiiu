#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class FrontendController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList ports READ ports NOTIFY portsChanged)
    Q_PROPERTY(int suggestedPort READ suggestedPort NOTIFY portsChanged)
  public:
    explicit FrontendController(QObject* parent = nullptr);
    QVariantList ports() const { return ports_; }
    int suggestedPort() const { return suggested_; }
    Q_INVOKABLE void refreshPorts();
  signals:
    void portsChanged();

  private:
    QVariantList ports_;
    int suggested_ = -1;
};
