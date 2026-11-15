#include "native_serial_writer.h"
#include <QMetaObject>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

struct NativeSerialWriter::State {
    static constexpr int InFlight = 8;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable wake;
    std::atomic_bool stop{false};
    QByteArray job;
    bool done = false;
    unsigned long error = 0;
    unsigned long writeBlock(const QByteArray& bytes);
    void cancel(const bool busy[]);
    HANDLE handle = INVALID_HANDLE_VALUE;
    HANDLE events[InFlight]{};
    OVERLAPPED operations[InFlight]{};
    DWORD counts[InFlight]{};
};

void NativeSerialWriter::State::cancel(const bool busy[])
{
    for (int i = 0; i < InFlight; ++i)
        if (busy[i])
        {
            DWORD written = 0;
            CancelIoEx(handle, &operations[i]);
            GetOverlappedResult(handle, &operations[i], &written, TRUE);
        }
}

unsigned long NativeSerialWriter::State::writeBlock(const QByteArray& bytes)
{
    DWORD error = 0;
    bool busy[InFlight]{};
    int pending = 0;
    qsizetype offset = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!error && (offset < bytes.size() || pending))
    {
        if (stop)
        {
            error = ERROR_OPERATION_ABORTED;
            break;
        }
        for (int i = 0; i < InFlight && offset < bytes.size() && !error; ++i)
        {
            if (busy[i])
                continue;
            operations[i] = {};
            operations[i].hEvent = reinterpret_cast<HANDLE>(quintptr(events[i]) | 1u);
            ResetEvent(events[i]);
            counts[i] = DWORD(std::min<qsizetype>(usb::PacketBytes, bytes.size() - offset));
            if (!WriteFile(handle, bytes.constData() + offset, counts[i], nullptr, &operations[i]) &&
                GetLastError() != ERROR_IO_PENDING)
            {
                error = GetLastError();
                break;
            }
            busy[i] = true;
            ++pending;
            offset += counts[i];
        }
        if (error || !pending)
            break;
        const DWORD wait = WaitForMultipleObjects(InFlight, events, FALSE, 50);
        if (wait == WAIT_TIMEOUT)
        {
            if (std::chrono::steady_clock::now() >= deadline)
                error = ERROR_TIMEOUT;
            continue;
        }
        for (int i = 0; i < InFlight; ++i)
        {
            if (!busy[i] || !HasOverlappedIoCompleted(&operations[i]))
                continue;
            DWORD written = 0;
            busy[i] = false;
            --pending;
            if (!GetOverlappedResult(handle, &operations[i], &written, FALSE))
                error = GetLastError();
            else if (written != counts[i])
                error = ERROR_WRITE_FAULT;
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        }
    }
    if (pending)
        cancel(busy);
    return error;
}

NativeSerialWriter::NativeSerialWriter(QSerialPort& port) : port_(port) {}
NativeSerialWriter::~NativeSerialWriter() { closeSession(); }

bool NativeSerialWriter::openSession()
{
    closeSession();
    if (!port_.isOpen())
    {
        setErrorString("Serial port is closed");
        return false;
    }
    failed_ = false;
    if (!open(QIODevice::WriteOnly | QIODevice::Unbuffered))
        return false;
    state_ = std::make_unique<State>();
    auto* state = state_.get();
    state->handle = reinterpret_cast<HANDLE>(port_.handle());
    for (auto& event : state->events)
        event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (std::find(std::begin(state->events), std::end(state->events), nullptr) !=
        std::end(state->events))
    {
        setErrorString("Cannot create native USB completion event");
        closeSession();
        return false;
    }
    const auto generation = generation_;
    state->worker = std::thread([this, state, generation] {
        while (true)
        {
            QByteArray bytes;
            {
                std::unique_lock<std::mutex> lock(state->mutex);
                state->wake.wait(
                    lock, [state] { return !state->job.isEmpty() || state->stop.load(); });
                if (state->stop)
                    break;
                bytes.swap(state->job);
            }
            const auto error = state->writeBlock(bytes);
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->error = error;
                state->done = true;
            }
            state->wake.notify_all();
            QMetaObject::invokeMethod(
                this, [this, generation] { finish(generation); }, Qt::QueuedConnection);
        }
    });
    return true;
}

void NativeSerialWriter::closeSession()
{
    ++generation_;
    if (state_)
    {
        state_->stop = true;
        state_->wake.notify_all();
        if (state_->worker.joinable())
            state_->worker.join();
        for (auto event : state_->events)
            if (event)
                CloseHandle(event);
        state_.reset();
    }
    inFlight_ = 0;
    close();
}

qint64 NativeSerialWriter::bytesToWrite() const
{
    return inFlight_;
}

qint64 NativeSerialWriter::writeData(const char* data, qint64 size)
{
    if (!state_ || failed_ || inFlight_ || size < 0 || size > usb::WorkerBlockBytes)
    {
        setErrorString("Native USB writer unavailable, busy or block too large");
        return -1;
    }
    if (!size)
        return 0;
    inFlight_ = size;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->job = QByteArray(data, size);
    }
    state_->wake.notify_all();
    return size;
}

void NativeSerialWriter::finish(quint64 generation)
{
    if (generation != generation_ || !state_)
        return;
    unsigned long error;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->done)
            return;
        error = state_->error;
        state_->done = false;
    }
    const auto count = inFlight_;
    inFlight_ = 0;
    if (error)
    {
        failed_ = true;
        setErrorString(QString("Native USB write failed: Windows error %1").arg(error));
        emit failed(errorString());
    } else if (count)
        emit bytesWritten(count);
}

bool NativeSerialWriter::waitForBytesWritten(int timeoutMs)
{
    if (!state_ || !inFlight_)
        return false;
    {
        std::unique_lock<std::mutex> lock(state_->mutex);
        if (timeoutMs < 0)
            state_->wake.wait(lock, [this] { return state_->done; });
        else if (!state_->wake.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                        [this] { return state_->done; }))
            return false;
    }
    finish(generation_);
    return !failed_;
}
