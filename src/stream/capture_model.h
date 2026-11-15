#pragma once
#include "video/capture_source.h"
#include "video/scene.h"
#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QString>
#include <QThread>
#include <QVariantList>
#include <atomic>
#include <functional>

namespace stream {
struct FrameRequest {
    video::Scene scene;
    bool frozen = false;
    QImage frozenImage;
    QByteArray frozenPixels;
    qint64 prepareAt = 0;
};
struct PreparedFrame {
    QImage image;
    QByteArray pixels;
    bool capture = false, frozen = false, reused = false;
    QSize sourceSize;
    quint64 serial = 0;
    qint64 prepareAt = 0, publishedUs = 0, requestedUs = 0, workerStart = 0, workerDone = 0,
           sourceUs = 0, convertUs = 0;
};

class FramePreparer : public QObject {
  public:
    explicit FramePreparer(video::CaptureSource& source);
    ~FramePreparer() override;
    void request(FrameRequest request, std::function<void(PreparedFrame)> complete);
    void cancel() { ++generation_; }

  private:
    class Worker;
    video::CaptureSource& source_;
    QThread thread_;
    Worker* worker_;
    std::atomic<quint64> generation_{0};
};

class CaptureModel : public QObject {
    Q_OBJECT
  public:
    explicit CaptureModel(QObject* parent = nullptr);
    ~CaptureModel() override;

    video::Scene& scene() { return scene_; }
    const video::Scene& scene() const { return scene_; }
    int mode() const { return scene_.mode; }
    bool paused() const { return paused_; }
    QVariantList targets() const { return targets_; }
    int target() const { return target_; }
    QString error() const { return error_; }
    bool sourceReady() const { return !scene_.capturing() || !image_.isNull(); }
    QImage preview() const { return scene_.capturing() ? image_ : video::render(scene_); }
    const QImage& image() const { return image_; }
    const QByteArray& pixels() const { return pixels_; }

    bool setMode(int mode);
    void setPaused(bool paused);
    void refresh();
    void select(int index);
    void selectDisplay(const QString& name);
    void cancel();
    void tick(qint64 now, bool streamBusy);

    FrameRequest request(qint64 now) const;
    FramePreparer& preparer() { return preparer_; }
    void keep(const QImage& image, const QByteArray& pixels);
    void fail(const QString& message);

  signals:
    void changed();
    void previewChanged();
    void targetsChanged();
    void failed(const QString& message);

  private:
    void clearImage();

    video::Scene scene_;
    video::CaptureSource source_;
    FramePreparer preparer_{source_};
    QVariantList targets_;
    int target_ = -1;
    QString error_;
    QImage image_;
    QByteArray pixels_;
    bool paused_ = false, previewJob_ = false;
    qint64 previewAt_ = 0;
};
} // namespace stream
