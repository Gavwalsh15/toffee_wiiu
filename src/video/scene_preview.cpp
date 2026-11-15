#include "scene_preview.h"
#include <QPainter>
void ScenePreview::setTransport(StreamController* value)
{
    if (transport_ == value)
        return;
    if (transport_)
        disconnect(transport_, nullptr, this, nullptr);
    transport_ = value;
    if (value)
    {
        connect(value, &StreamController::previewChanged, this, [this] {
            if (live_)
                update();
        });
        connect(value, &QObject::destroyed, this, [this] {
            transport_ = nullptr;
            update();
        });
    }
    update();
    emit transportChanged();
}
void ScenePreview::paint(QPainter* painter)
{
    if (transport_)
        painter->drawImage(boundingRect(), transport_->previewImage());
}
