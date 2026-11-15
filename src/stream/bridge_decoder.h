#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace stream {
struct BridgeRecord {
    std::uint8_t type = 0;
    std::uint32_t sequence = 0;
    std::vector<std::uint8_t> payload;
};

class BridgeDecoder {
  public:
    void feed(const char* data, std::size_t size, std::string& text,
              std::vector<BridgeRecord>& records);
    std::uint64_t corrupt() const { return corrupt_; }
    void reset() { pending_.clear(); }

  private:
    std::string pending_;
    std::uint64_t corrupt_ = 0;
};
} // namespace stream
