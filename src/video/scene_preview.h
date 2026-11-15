#pragma once
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>
#include "stream/stream_controller.h"
class ScenePreview : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(
        StreamController* transport READ transport WRITE setTransport NOTIFY transportChanged)
    Q_PROPERTY(bool live READ live WRITE setLive NOTIFY liveChanged)
  public:
    using QQuickPaintedItem::QQuickPaintedItem;
    StreamController* transport() const { return transport_; }
    void setTransport(StreamController* value);
    bool live() const { return live_; }
    void setLive(bool value)
    {
        if (live_ == value)
            return;
        live_ = value;
        if (live_)
            update();
        emit liveChanged();
    }
    void paint(QPainter* painter) override;
  signals:
    void transportChanged();
    void liveChanged();

  private:
    StreamController* transport_ = nullptr;
    bool live_ = true;
};
