#include "native_capture.h"
#include <QThread>
#include <QElapsedTimer>
#include <atomic>
#include <cstring>
#include "common/com_ptr.h"
#include "common/pacing_trace.h"
#include "common/video_profile.h"
#include "common/monotonic_clock.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

namespace video {
namespace profile = toffee::video;
namespace {
struct GpuScale {
    Com<ID3D11VideoDevice> device;
    Com<ID3D11VideoContext> context;
    Com<ID3D11VideoProcessorEnumerator> enumerator;
    Com<ID3D11VideoProcessor> processor;
    Com<ID3D11Texture2D> input, output;
    Com<ID3D11VideoProcessorInputView> inputView;
    Com<ID3D11VideoProcessorOutputView> outputView;
    QSize size, monitor;
    QRect lastSource;
    bool setSource(QRect region)
    {
        region = region.intersected(QRect(QPoint(0, 0), monitor));
        region.setSize(QSize(region.width() & ~1, region.height() & ~1));
        if (region.width() < 16 || region.height() < 16)
            return false;
        if (region == lastSource)
            return true;
        lastSource = region;
        const auto fitted = region.size().scaled(size, Qt::KeepAspectRatio);
        const int w = fitted.width() & ~1, h = fitted.height() & ~1;
        const int x = ((size.width() - w) / 2) & ~1, y = ((size.height() - h) / 2) & ~1;
        RECT from{region.left(), region.top(), region.left() + region.width(),
                  region.top() + region.height()};
        RECT to{x, y, x + w, y + h};
        context->VideoProcessorSetStreamSourceRect(processor.p, 0, TRUE, &from);
        context->VideoProcessorSetStreamDestRect(processor.p, 0, TRUE, &to);
        return true;
    }
    bool init(ID3D11Device* d, ID3D11DeviceContext* c, QSize source, bool fixedOutput = false)
    {
        monitor = source;
        auto fitted = source.scaled(QSize(profile::WIDTH, profile::HEIGHT), Qt::KeepAspectRatio);
        size = fixedOutput ? QSize(profile::WIDTH & ~1, profile::HEIGHT & ~1)
                           : QSize(fitted.width() & ~1, fitted.height() & ~1);
        if (size.width() < 2 || size.height() < 2)
            return false;
        if (FAILED(d->QueryInterface(__uuidof(ID3D11VideoDevice),
                                     reinterpret_cast<void**>(device.put()))) ||
            FAILED(c->QueryInterface(__uuidof(ID3D11VideoContext),
                                     reinterpret_cast<void**>(context.put()))))
            return false;
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
        content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        content.InputFrameRate = {60, 1};
        content.OutputFrameRate = {60, 1};
        content.InputWidth = source.width();
        content.InputHeight = source.height();
        content.OutputWidth = size.width();
        content.OutputHeight = size.height();
        content.Usage = D3D11_VIDEO_USAGE_OPTIMAL_QUALITY;
        if (FAILED(device->CreateVideoProcessorEnumerator(&content, enumerator.put())) ||
            FAILED(device->CreateVideoProcessor(enumerator.p, 0, processor.put())))
        {
            content.Usage = D3D11_VIDEO_USAGE_OPTIMAL_SPEED;
            if (FAILED(device->CreateVideoProcessorEnumerator(&content, enumerator.put())) ||
                FAILED(device->CreateVideoProcessor(enumerator.p, 0, processor.put())))
                return false;
        }
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width = source.width();
        texture.Height = source.height();
        texture.MipLevels = 1;
        texture.ArraySize = 1;
        texture.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        texture.SampleDesc.Count = 1;
        texture.Usage = D3D11_USAGE_DEFAULT;
        texture.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(d->CreateTexture2D(&texture, nullptr, input.put())))
            return false;
        texture.Width = size.width();
        texture.Height = size.height();
        if (FAILED(d->CreateTexture2D(&texture, nullptr, output.put())))
            return false;
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC in{};
        in.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC out{};
        out.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        if (FAILED(device->CreateVideoProcessorInputView(input.p, enumerator.p, &in,
                                                         inputView.put())) ||
            FAILED(device->CreateVideoProcessorOutputView(output.p, enumerator.p, &out,
                                                          outputView.put())))
            return false;
        RECT from{0, 0, source.width(), source.height()}, to{0, 0, size.width(), size.height()};
        context->VideoProcessorSetStreamFrameFormat(processor.p, 0,
                                                    D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        context->VideoProcessorSetStreamSourceRect(processor.p, 0, TRUE, &from);
        context->VideoProcessorSetStreamDestRect(processor.p, 0, TRUE, &to);
        context->VideoProcessorSetOutputTargetRect(processor.p, TRUE, &to);
        D3D11_VIDEO_COLOR black{};
        black.RGBA.A = 1.0f;
        context->VideoProcessorSetOutputBackgroundColor(processor.p, FALSE, &black);
        context->VideoProcessorSetStreamAutoProcessingMode(processor.p, 0, FALSE);
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE rgb{};
        rgb.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
        context->VideoProcessorSetStreamColorSpace(processor.p, 0, &rgb);
        context->VideoProcessorSetOutputColorSpace(processor.p, &rgb);
        return true;
    }
    bool blit()
    {
        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = inputView.p;
        return SUCCEEDED(context->VideoProcessorBlt(processor.p, outputView.p, 0, 1, &stream));
    }
};
struct Target {
    Com<IDXGIAdapter1> adapter;
    Com<IDXGIOutput1> output;
    QSize size;
};

const char* findTarget(const QString& device, Target& target)
{
    Com<IDXGIFactory1> factory;
    if (FAILED(
            CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.put()))))
        return "DXGI factory creation failed";
    unsigned matches = 0;
    for (unsigned ai = 0;; ++ai)
    {
        Com<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(ai, adapter.put()) == DXGI_ERROR_NOT_FOUND)
            break;
        if (!adapter.p)
            continue;
        for (unsigned oi = 0;; ++oi)
        {
            Com<IDXGIOutput> output;
            if (adapter->EnumOutputs(oi, output.put()) == DXGI_ERROR_NOT_FOUND)
                break;
            if (!output.p)
                continue;
            DXGI_OUTPUT_DESC desc{};
            if (FAILED(output->GetDesc(&desc)) || !desc.AttachedToDesktop)
                continue;
            // The device name is unique, unlike a display's size.
            if (QString::fromWCharArray(desc.DeviceName) != device)
                continue;
            target.size = QSize(desc.DesktopCoordinates.right - desc.DesktopCoordinates.left,
                                desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top);
            ++matches;
            if (FAILED(output->QueryInterface(__uuidof(IDXGIOutput1),
                                              reinterpret_cast<void**>(target.output.put()))))
                continue;
            *target.adapter.put() = adapter.p;
            adapter.p->AddRef();
        }
    }
    if (matches != 1 || !target.output.p || !target.adapter.p)
        return "Native capture could not find this display";
    return nullptr;
}

class DisplayReader {
  public:
    struct Timing {
        qint64 acquireAt = 0, acquired = 0, released = 0, scaled = 0, mappedAt = 0;
        unsigned accumulatedFrames = 0;
    };
    enum class Status {
        Picture,  // `image` holds a new picture
        NoChange, // nothing on the display changed
        Failed,   // `error` says why; the capture cannot go on
    };

    const char* open(const Target& target, bool windowCapture)
    {
        size_ = target.size;
        D3D_FEATURE_LEVEL level;
        if (FAILED(D3D11CreateDevice(target.adapter.p, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                     D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                     D3D11_SDK_VERSION, device_.put(), &level, context_.put())))
            return "D3D capture device creation failed";
        if (FAILED(target.output->DuplicateOutput(device_.p, duplication_.put())))
            return "Desktop duplication failed";
        DXGI_OUTDUPL_DESC outputSpec{};
        duplication_->GetDesc(&outputSpec);
        if (outputSpec.Rotation != DXGI_MODE_ROTATION_IDENTITY)
            return "Native capture currently requires an unrotated display";
        gpuScale_ = scaler_.init(device_.p, context_.p, size_, windowCapture);
        if (windowCapture && !gpuScale_)
            return "Window capture needs GPU scaling, which is unavailable";
        readSize_ = gpuScale_ ? scaler_.size : size_;
        return nullptr;
    }

    bool showRegion(const QRect& region) { return scaler_.setSource(region); }

    Status read(QImage& image, Timing& timing, const char*& error)
    {
        timing.acquireAt = pacing::nowUs();
        DXGI_OUTDUPL_FRAME_INFO info{};
        Com<IDXGIResource> resource;
        const auto result = duplication_->AcquireNextFrame(16, &info, resource.put());
        if (result == DXGI_ERROR_WAIT_TIMEOUT)
            return Status::NoChange;
        if (FAILED(result))
            return failed(error, "Desktop capture was lost; reselect the capture source");
        timing.acquired = pacing::nowUs();
        timing.accumulatedFrames = info.AccumulatedFrames;

        Com<ID3D11Texture2D> texture;
        if (FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),
                                            reinterpret_cast<void**>(texture.put()))))
        {
            duplication_->ReleaseFrame();
            return failed(error, "Desktop texture is unavailable");
        }
        D3D11_TEXTURE2D_DESC spec{};
        texture->GetDesc(&spec);
        if (spec.Format != DXGI_FORMAT_B8G8R8A8_UNORM || int(spec.Width) != size_.width() ||
            int(spec.Height) != size_.height())
        {
            duplication_->ReleaseFrame();
            return failed(error, "Native desktop format changed; reselect the source");
        }
        if (!staging_.p)
        {
            spec.Width = readSize_.width();
            spec.Height = readSize_.height();
            spec.Usage = D3D11_USAGE_STAGING;
            spec.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            spec.MiscFlags = 0;
            spec.BindFlags = 0;
            if (FAILED(device_->CreateTexture2D(&spec, nullptr, staging_.put())))
            {
                duplication_->ReleaseFrame();
                return failed(error, "Capture readback allocation failed");
            }
        }
        context_->CopyResource(gpuScale_ ? scaler_.input.p : staging_.p, texture.p);
        if (FAILED(duplication_->ReleaseFrame()))
            return failed(error, "Desktop frame release failed");
        timing.released = pacing::nowUs();

        if (gpuScale_)
        {
            if (!scaler_.blit())
                return failed(error, "GPU capture scaling failed");
            context_->CopyResource(staging_.p, scaler_.output.p);
        }
        timing.scaled = pacing::nowUs();

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging_.p, 0, D3D11_MAP_READ, 0, &mapped)))
            return failed(error, "Capture readback failed");
        timing.mappedAt = pacing::nowUs();
        image = QImage(readSize_, QImage::Format_RGB32);
        if (!image.isNull())
            for (int y = 0; y < readSize_.height(); ++y)
                std::memcpy(image.scanLine(y),
                            static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch,
                            readSize_.width() * 4);
        context_->Unmap(staging_.p, 0);
        if (image.isNull())
            return failed(error, "Capture image allocation failed");
        return Status::Picture;
    }

  private:
    static Status failed(const char*& error, const char* why)
    {
        error = why;
        return Status::Failed;
    }
    Com<ID3D11Device> device_;
    Com<ID3D11DeviceContext> context_;
    Com<IDXGIOutputDuplication> duplication_;
    Com<ID3D11Texture2D> staging_;
    GpuScale scaler_;
    bool gpuScale_ = false;
    QSize size_, readSize_;
};
} // namespace

struct NativeCapture::Impl : QThread {
    std::atomic_bool stopping{false};
    QString device;
    std::function<void(QImage)> publish;
    std::function<void(QString)> failure;
    NativeCapture::Crop crop;
    void fail(const char* text)
    {
        if (!stopping.load())
            failure(QString::fromLatin1(text));
    }
    void run() override
    {
        Target target;
        if (const char* why = findTarget(device, target))
            return fail(why);
        DisplayReader reader;
        if (const char* why = reader.open(target, bool(crop)))
            return fail(why);

        QElapsedTimer clock;
        clock.start();
        pacing::Trace trace("capture");
        qint64 next = 0;
        while (!stopping.load())
        {
            const auto wait = next - clock.nsecsElapsed() / 1000;
            if (wait > 0)
                QThread::usleep(static_cast<unsigned long>(wait));
            if (stopping.load())
                break;
            const auto start = clock.nsecsElapsed() / 1000;
            const auto late = next ? std::max<qint64>(0, start - next) : 0;
            next = pacing::advanceFrameDeadline(next, start);
            if (crop)
            {
                QRect region;
                if (!crop(region) || !reader.showRegion(region))
                    continue; // keep the last picture
            }
            QImage image;
            DisplayReader::Timing timing;
            const char* error = nullptr;
            const auto status = reader.read(image, timing, error);
            if (status == DisplayReader::Status::NoChange)
                continue;
            if (status == DisplayReader::Status::Failed)
            {
                fail(error);
                break;
            }
            if (!stopping.load())
                publish(std::move(image));
            if (trace.enabled())
                trace.add(QString("event=capture at_us=%1 acquired_us=%2 acquire_us=%3 "
                                  "release_us=%4 scale_us=%5 map_us=%6 copy_us=%7 wake_late_us=%8 "
                                  "accumulated=%9")
                              .arg(pacing::nowUs())
                              .arg(timing.acquired)
                              .arg(timing.acquired - timing.acquireAt)
                              .arg(timing.released - timing.acquired)
                              .arg(timing.scaled - timing.released)
                              .arg(timing.mappedAt - timing.scaled)
                              .arg(pacing::nowUs() - timing.mappedAt)
                              .arg(late)
                              .arg(timing.accumulatedFrames)
                              .toUtf8());
        }
    }
};

NativeCapture::NativeCapture() : impl_(std::make_unique<Impl>()) {}
NativeCapture::~NativeCapture() { stop(); }
void NativeCapture::stop()
{
    impl_->stopping.store(true);
    impl_->wait();
}
void NativeCapture::start(const QString& device, std::function<void(QImage)> frame,
                          std::function<void(QString)> error, Crop crop)
{
    stop();
    impl_->crop = std::move(crop);
    impl_->device = device;
    impl_->publish = std::move(frame);
    impl_->failure = std::move(error);
    impl_->stopping.store(false);
    impl_->start();
}
} // namespace video
