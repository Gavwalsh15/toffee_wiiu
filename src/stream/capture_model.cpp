#include "capture_model.h"
#include "common/monotonic_clock.h"
#include "video/capture_converter.h"
#include <QElapsedTimer>

namespace stream {
class FramePreparer::Worker : public QObject {
  public:
    video::CaptureConverter converter;
};
FramePreparer::FramePreparer(video::CaptureSource& source) : source_(source), worker_(new Worker)
{
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    thread_.start();
}
FramePreparer::~FramePreparer()
{
    cancel();
    thread_.quit();
    thread_.wait();
}
void FramePreparer::request(FrameRequest request, std::function<void(PreparedFrame)> complete)
{
    const auto generation = generation_.load();
    const auto requestedUs = pacing::nowUs();
    QMetaObject::invokeMethod(
        worker_,
        [this, generation, request = std::move(request), complete = std::move(complete),
         requestedUs]() mutable {
            if (generation != generation_.load())
                return;
            PreparedFrame frame;
            frame.requestedUs = requestedUs;
            frame.workerStart = pacing::nowUs();
            frame.prepareAt = request.prepareAt;
            frame.capture = request.scene.capturing();
            frame.frozen = request.frozen;
            QElapsedTimer stages;
            stages.start();
            if (frame.capture && !frame.frozen)
            {
                const auto snapshot = source_.snapshot();
                const auto prepared = worker_->converter.prepare(snapshot.image, snapshot.serial);
                frame.image = prepared.image;
                frame.pixels = prepared.pixels;
                frame.sourceUs = prepared.sourceUs;
                frame.convertUs = prepared.convertUs;
                frame.reused = prepared.reused;
                frame.sourceSize = prepared.sourceSize;
                frame.serial = snapshot.serial;
                frame.publishedUs = snapshot.publishedUs;
            } else
            {
                frame.image = frame.capture ? request.frozenImage : video::render(request.scene);
                frame.sourceUs = stages.nsecsElapsed() / 1000;
                if (frame.capture && frame.frozen && !request.frozenPixels.isEmpty())
                    frame.pixels = request.frozenPixels;
                else if (!frame.image.isNull())
                    frame.pixels = video::i420(frame.image);
                frame.convertUs = stages.nsecsElapsed() / 1000 - frame.sourceUs;
            }
            frame.workerDone = pacing::nowUs();
            QMetaObject::invokeMethod(
                this,
                [this, generation, frame = std::move(frame),
                 complete = std::move(complete)]() mutable {
                    if (generation == generation_.load())
                        complete(std::move(frame));
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

namespace {
constexpr qint64 PreviewPeriodUs = 33367; // ~30 fps
}

CaptureModel::CaptureModel(QObject* parent) : QObject(parent)
{
    video::render(scene_);
    connect(&source_, &video::CaptureSource::failed, this, [this](const QString& message) {
        error_ = message;
        source_.stop();
        clearImage();
        emit failed(message);
        emit targetsChanged();
        emit previewChanged();
    });
}

CaptureModel::~CaptureModel()
{
    source_.stop();
    preparer_.cancel();
}

void CaptureModel::clearImage()
{
    image_ = {};
    pixels_.clear();
}

bool CaptureModel::setMode(int mode)
{
    if (mode < video::Scene::Interactive || mode > video::Scene::WindowCapture ||
        scene_.mode == mode)
        return false;
    scene_.mode = mode;
    paused_ = false;
    refresh();
    emit changed();
    emit previewChanged();
    return true;
}

void CaptureModel::setPaused(bool paused)
{
    if (paused_ == paused)
        return;
    paused_ = paused;
    emit changed();
}

void CaptureModel::cancel()
{
    preparer_.cancel();
    previewJob_ = false;
}

void CaptureModel::refresh()
{
    cancel();
    source_.stop();
    clearImage();
    error_.clear();
    targets_ = source_.refresh(scene_.mode);
    target_ = -1;
    if (!targets_.isEmpty())
        select(0);
    emit targetsChanged();
    emit previewChanged();
}

void CaptureModel::select(int index)
{
    if (!scene_.capturing())
        return;
    cancel();
    clearImage();
    error_.clear();
    target_ = index;
    if (!source_.select(scene_.mode, index))
        error_ = "Choose an available capture source, or refresh the list.";
    emit targetsChanged();
    emit previewChanged();
}

void CaptureModel::selectDisplay(const QString& name)
{
    if (name.isEmpty())
        return;
    if (scene_.mode != video::Scene::DisplayCapture)
        setMode(video::Scene::DisplayCapture);
    else
        refresh(); // the display may have appeared since the list was made
    for (int i = 0; i < targets_.size(); ++i)
        if (targets_[i].toMap().value("label").toString().startsWith(name))
            return select(i);
}

void CaptureModel::tick(qint64 now, bool streamBusy)
{
    if (now - previewAt_ < PreviewPeriodUs)
        return;
    previewAt_ = now;
    emit previewChanged();
    if (!scene_.capturing() || streamBusy || previewJob_ || (paused_ && !image_.isNull()) ||
        !source_.hasFrame())
        return;
    previewJob_ = true;
    FrameRequest request;
    request.scene = scene_;
    preparer_.request(std::move(request), [this](PreparedFrame frame) {
        previewJob_ = false;
        if (paused_ && !image_.isNull())
            return;
        keep(frame.image, frame.pixels);
        emit previewChanged();
    });
}

FrameRequest CaptureModel::request(qint64 now) const
{
    FrameRequest request;
    request.scene = scene_;
    request.frozen = paused_;
    request.frozenImage = image_;
    request.frozenPixels = pixels_;
    request.prepareAt = now;
    return request;
}

void CaptureModel::keep(const QImage& image, const QByteArray& pixels)
{
    image_ = image;
    pixels_ = pixels;
}

void CaptureModel::fail(const QString& message)
{
    error_ = message;
    emit targetsChanged();
}
} // namespace stream
