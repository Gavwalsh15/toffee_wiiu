#include "bridge_decoder.h"
#include "common/bridge_record.h"
#include <algorithm>
#include <cstring>

namespace stream {
void BridgeDecoder::feed(const char* data, std::size_t size, std::string& text,
                         std::vector<BridgeRecord>& records)
{
    pending_.append(data, size);
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(pending_.data());
    const std::size_t total = pending_.size();
    std::size_t pos = 0;
    while (pos < total)
    {
        const auto* start = static_cast<const std::uint8_t*>(
            std::memchr(bytes + pos, BRIDGE_MAGIC[0], total - pos));
        if (!start)
        {
            text.append(pending_, pos, std::string::npos);
            pos = total;
            break;
        }
        const std::size_t at = std::size_t(start - bytes);
        text.append(pending_, pos, at - pos);
        pos = at;
        const std::size_t available = total - pos;
        // The byte is text, and a record may still start with the next one.
        if (std::memcmp(bytes + pos, BRIDGE_MAGIC, std::min<std::size_t>(available, 4)) != 0)
        {
            text.push_back(pending_[pos++]);
            continue;
        }
        if (available < RECORD_HEADER_SIZE)
            break; // wait for the rest of the header
        const std::uint8_t* header = bytes + pos;
        const std::uint32_t length = record_get_u32(header + 12);
        if (header[4] < BRIDGE_HELLO || header[5] || header[6] || header[7] ||
            length > BRIDGE_MAX_PAYLOAD)
        {
            text.push_back(pending_[pos++]);
            continue;
        }
        if (available < RECORD_HEADER_SIZE + length)
            break; // wait for the rest of the payload
        const std::uint8_t* payload = header + RECORD_HEADER_SIZE;
        if (toffee_crc32(payload, length) != record_get_u32(header + 16))
        {
            ++corrupt_;
            text.push_back(pending_[pos++]);
            continue;
        }
        BridgeRecord record;
        record.type = header[4];
        record.sequence = record_get_u32(header + 8);
        record.payload.assign(payload, payload + length);
        records.push_back(std::move(record));
        pos += RECORD_HEADER_SIZE + length;
    }
    pending_.erase(0, pos);
}
} // namespace stream
