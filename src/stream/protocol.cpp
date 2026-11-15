#include "protocol.h"
#include "common/video_profile.h"
#include "common/drh_record.h"
#include "common/bridge_record.h"
#include <QStringList>
#include <algorithm>
namespace stream {
namespace profile = toffee::video;

QByteArray packet(quint8 type, quint32 sequence, const QByteArray& payload)
{
    QByteArray out(qsizetype(RECORD_HEADER_SIZE) + payload.size(), Qt::Uninitialized);
    record_encode(reinterpret_cast<uint8_t*>(out.data()), "TVU1", type, sequence, payload.constData(),
                  size_t(payload.size()));
    return out;
}
QByteArray beginPayload()
{
    QByteArray out("TOFFEE-DRH-LOOP\0", 16);
    for (quint32 value : {profile::WIDTH, profile::HEIGHT, 60u, 0xffffffffu})
        append32(out, value);
    return out;
}
bool validRecord(const QByteArray& record, bool idr)
{
    drh_picture_t picture;
    return drh_parse_picture(reinterpret_cast<const uint8_t*>(record.constData()),
                             size_t(record.size()), &picture, nullptr) == DRH_RECORD_OK &&
           picture.idr == idr;
}
void Feedback::consume(const QByteArray& data)
{
    std::string text_;
    std::vector<BridgeRecord> records;
    decoder.feed(data.constData(), size_t(data.size()), text_, records);
    text = QByteArray::fromStdString(text_);
    inputs.clear();
    for (const auto& record : records)
        apply(record);
}

void Feedback::apply(const BridgeRecord& record)
{
    const auto& payload = record.payload;
    switch (record.type)
    {
    case BRIDGE_HELLO:
    {
        if (payload.size() < 8)
            break;
        const unsigned version = payload[0] | (payload[1] << 8);
        if (version != BRIDGE_PROTOCOL_VERSION)
        {
            incompatible = QString("The bridge firmware speaks protocol %1 and this app protocol %2. "
                                   "Flash the firmware from the Flash ESP page.")
                        .arg(version)
                        .arg(BRIDGE_PROTOCOL_VERSION);
            break;
        }
        const quint32 id = record_get_u32(payload.data() + 4);
        if (hello && id != bootId)
        {
            ready = false;
            profile = -1;
            completed = -1;
            reset();
            ++boots;
        }
        hello = true;
        bootId = id;
        break;
    }
    case BRIDGE_STATE:
        if (payload.empty())
            break;
        if (ready && !payload[0])
            idr = true;
        ready = payload[0] != 0;
        pairingCode = payload.size() >= 3 && payload[1] ? payload[2] : -1;
        break;
    case BRIDGE_FRAME_DONE:
        completed = std::max(completed, qint64(record.sequence));
        completedThrough(completed);
        break;
    case BRIDGE_STREAM_READY:
        profile = int(record.sequence);
        reset();
        break;
    case BRIDGE_IDR_REQUEST:
        request();
        break;
    case BRIDGE_IDR_SENT:
        if (profile == Profile)
            submitted(record.sequence);
        break;
    case BRIDGE_ERROR:
        error = QString::fromUtf8(reinterpret_cast<const char*>(payload.data()), int(payload.size()));
        break;
    case BRIDGE_INPUT:
        inputs.append({record.sequence, QByteArray(reinterpret_cast<const char*>(payload.data()),
                                                   qsizetype(payload.size()))});
        break;
    default:
        break; // a record from a newer bridge that this app does not use
    }
}
QString pairingSymbols(int code)
{
    QString text;
    for (int shift = 6; code >= 0 && shift >= 0; shift -= 2)
        text += QChar(u"\u2660\u2665\u2666\u2663"[(code >> shift) & 3]);
    return text;
}
QString pairingNames(int code)
{
    static const char* const names[] = {"spade", "heart", "diamond", "club"};
    QStringList text;
    for (int shift = 6; code >= 0 && shift >= 0; shift -= 2)
        text << names[(code >> shift) & 3];
    return text.join(' ');
}
} // namespace stream
