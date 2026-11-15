#include "audio_capture.h"
#include "common/com_ptr.h"
#include <chrono>
#include <cmath>
#include <cstdint>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmsystem.h>
#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif

namespace {
constexpr REFERENCE_TIME BufferDuration = 1000000;
constexpr auto PollInterval = std::chrono::milliseconds(2);
constexpr auto RetryInterval = std::chrono::milliseconds(1000);

inline int16_t boost(int16_t sample, float gain)
{
    constexpr float Knee = 0.75f;
    const float x = sample / 32768.0f * gain;
    const float magnitude = std::fabs(x);
    const float y =
        magnitude <= Knee ? magnitude : Knee + (1.0f - Knee) * std::tanh((magnitude - Knee) / (1.0f - Knee));
    return int16_t(std::lround(std::copysign(y, x) * 32767.0f));
}
} // namespace

AudioCapture::AudioCapture(QObject* parent) : QObject(parent) {}
AudioCapture::~AudioCapture() { stop(); }

void AudioCapture::start()
{
    if (thread_.joinable())
        return;
    stop_ = false;
    thread_ = std::thread([this] { run(); });
}

void AudioCapture::stop()
{
    if (!thread_.joinable())
        return;
    stop_ = true;
    thread_.join();
}

void AudioCapture::run()
{
    const bool comReady = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    timeBeginPeriod(1);
    QByteArray pending;
    bool reported = false;
    while (!stop_)
    {
        Com<IMMDeviceEnumerator> enumerator;
        Com<IMMDevice> device;
        Com<IAudioClient> client;
        Com<IAudioCaptureClient> capture;

        constexpr DWORD rate = 24000;
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 2;
        format.nSamplesPerSec = rate;
        format.wBitsPerSample = 16;
        format.nBlockAlign = 4;
        format.nAvgBytesPerSec = rate * 4;
        HRESULT hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                      IID_IMMDeviceEnumerator,
                                      reinterpret_cast<void**>(enumerator.put()));
        if (SUCCEEDED(hr))
            hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.put());
        if (SUCCEEDED(hr))
            hr = device->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(client.put()));
        if (SUCCEEDED(hr))
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                    AUDCLNT_STREAMFLAGS_LOOPBACK |
                                        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                    BufferDuration, 0, &format, nullptr);
        if (SUCCEEDED(hr))
            hr = client->GetService(IID_IAudioCaptureClient, reinterpret_cast<void**>(capture.put()));
        if (SUCCEEDED(hr))
            hr = client->Start();
        if (FAILED(hr))
        {
            if (!reported)
                emit failed(QString("Audio capture unavailable (0x%1)")
                                .arg(quint32(hr), 8, 16, QLatin1Char('0')));
            reported = true;
            for (int i = 0; i < 10 && !stop_; ++i)
                std::this_thread::sleep_for(RetryInterval / 10);
            continue;
        }
        reported = false;
        pending.clear();
        while (!stop_ && SUCCEEDED(hr))
        {
            UINT32 frames = 0;
            while (SUCCEEDED(hr = capture->GetNextPacketSize(&frames)) && frames)
            {
                BYTE* data = nullptr;
                DWORD flags = 0;
                hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
                if (FAILED(hr))
                    break;
                const int bytes = int(frames) * format.nBlockAlign;
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                    pending.append(bytes, '\0');
                else
                {
                    const float gain = gain_;
                    const auto* samples = reinterpret_cast<const int16_t*>(data);
                    const int count = bytes / 2;
                    const int start = pending.size();
                    pending.resize(start + bytes);
                    auto* out = reinterpret_cast<int16_t*>(pending.data() + start);
                    for (int i = 0; i < count; ++i)
                        out[i] = gain == 1.0f ? samples[i] : boost(samples[i], gain);
                }
                capture->ReleaseBuffer(frames);
                while (pending.size() >= int(AUDIO_CHUNK_BYTES))
                {
                    emit chunk(pending.left(AUDIO_CHUNK_BYTES));
                    pending.remove(0, AUDIO_CHUNK_BYTES);
                }
            }
            std::this_thread::sleep_for(PollInterval);
        }
        client->Stop();
    }
    timeEndPeriod(1);
    if (comReady)
        CoUninitialize();
}
