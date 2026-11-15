#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <QtEndian>
#include <cstdint>
#include "bridge_decoder.h"
#include "common/video_profile.h"
#include "common/wire_limits.h"
namespace stream {
constexpr int Profile = toffee::video::STREAM_PROFILE;
inline quint32 u32(const char* p) { return qFromLittleEndian<quint32>(p); }
inline void append32(QByteArray& bytes, quint32 value)
{
    char data[4];
    qToLittleEndian(value, data);
    bytes.append(data, 4);
}
QByteArray packet(quint8 type, quint32 sequence, const QByteArray& payload = {});
QByteArray beginPayload();
bool validRecord(const QByteArray& record, bool idr);
// A pairing code (common/wire_limits.h) as its four card symbols, or as their names.
QString pairingSymbols(int code);
QString pairingNames(int code);

struct RecoveryPolicy {
    bool idr = true;
    std::int64_t lastIdr = -1, pendingIdr = -1;
    std::uint64_t requests = 0;
    void reset()
    {
        idr = true;
        lastIdr = pendingIdr = -1;
    }
    void request()
    {
        idr = true;
        ++requests;
    }
    bool chooseIdr(std::uint32_t sequence, bool gap)
    {
        if (pendingIdr >= 0 || (!idr && !gap))
            return false;
        pendingIdr = sequence;
        idr = false;
        return true;
    }
    void submitted(std::int64_t sequence)
    {
        if (sequence <= lastIdr)
            return;
        lastIdr = sequence;
        if (pendingIdr < 0 || sequence >= pendingIdr)
        {
            pendingIdr = -1;
            idr = false;
        }
    }
    void completedThrough(std::int64_t sequence)
    {
        if (pendingIdr >= 0 && sequence >= pendingIdr)
        {
            pendingIdr = -1;
            idr = true;
        }
    }
};

// What the bridge has told us (common/bridge_record.h), folded into one picture of its state.
struct Feedback : RecoveryPolicy {
    struct InputReport {
        quint32 sequence;
        QByteArray bytes;
    };
    bool ready = false;
    int pairingCode = -1;
    int profile = -1;
    qint64 completed = -1;
    quint64 boots = 0;
    bool hello = false;
    quint32 bootId = 0;
    QString error;
    QString incompatible;
    QByteArray text;
    QList<InputReport> inputs;
    BridgeDecoder decoder;
    void consume(const QByteArray& data);
    void beginSession()
    {
        profile = -1;
        completed = -1;
        reset();
    }
    bool canSend(quint32 sequence) const
    {
        return qint64(sequence) <= completed + MAX_OUTSTANDING_RECORDS;
    }

  private:
    void apply(const BridgeRecord& record);
};
} // namespace stream
