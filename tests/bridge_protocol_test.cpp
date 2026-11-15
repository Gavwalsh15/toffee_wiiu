#include "check.h"
#include "common/bridge_record.h"
#include "common/wire_limits.h"
#include "picture_ring.h"   // firmware/src/transport
#include "record_parser.h" // firmware/src/transport
#include "stream/bridge_decoder.h"
#include <algorithm>
#include <cstdio>
#include <string>

namespace {
std::string record(std::uint8_t type, std::uint32_t sequence, const std::string& payload = {})
{
    std::uint8_t out[RECORD_HEADER_SIZE + BRIDGE_MAX_PAYLOAD];
    const auto size = bridge_encode(out, type, sequence, payload.data(), payload.size());
    return std::string(reinterpret_cast<char*>(out), size);
}

struct Decoded {
    std::string text;
    std::vector<stream::BridgeRecord> records;
};

Decoded decode(stream::BridgeDecoder& decoder, const std::string& bytes, std::size_t piece)
{
    Decoded result;
    for (std::size_t at = 0; at < bytes.size(); at += piece)
        decoder.feed(bytes.data() + at, std::min(piece, bytes.size() - at), result.text,
                     result.records);
    return result;
}

void crc()
{
    CHECK(toffee_crc32("123456789", 9) == 0xcbf43926u);
    CHECK(toffee_crc32(nullptr, 0) == 0);
}

void roundTrip()
{
    const std::string input(BRIDGE_INPUT_REPORT_BYTES, '\x5a');
    const std::string stream = record(BRIDGE_HELLO, 0, std::string("\x02\x00\x00\x00\x78\x56\x34\x12", 8)) +
                               record(BRIDGE_STATE, 0, std::string("\x01\0\0\0", 4)) +
                               record(BRIDGE_FRAME_DONE, 41) + record(BRIDGE_STREAM_READY, 5) +
                               record(BRIDGE_IDR_REQUEST, 0) + record(BRIDGE_IDR_SENT, 40) +
                               record(BRIDGE_ERROR, 0, "out of memory") +
                               record(BRIDGE_INPUT, 0xfffffffeu, input);
    stream::BridgeDecoder decoder;
    const auto result = decode(decoder, stream, stream.size());
    CHECK(result.text.empty());
    CHECK(result.records.size() == 8);
    if (result.records.size() != 8)
        return;
    CHECK(result.records[0].type == BRIDGE_HELLO && result.records[0].payload.size() == 8);
    CHECK(result.records[2].type == BRIDGE_FRAME_DONE && result.records[2].sequence == 41);
    CHECK(result.records[3].sequence == 5);
    CHECK(std::string(result.records[6].payload.begin(), result.records[6].payload.end()) ==
          "out of memory");
    CHECK(result.records[7].sequence == 0xfffffffeu && result.records[7].payload.size() == 128);
    CHECK(decoder.corrupt() == 0);
}

void textAndRecordsInterleaved()
{
    const std::string stream = "I (100) boot: starting\r\n" + record(BRIDGE_FRAME_DONE, 1) +
                               "I (101) video: PC_STREAM_READY:5\r\n" + record(BRIDGE_FRAME_DONE, 2) +
                               "tail";
    const std::string text = "I (100) boot: starting\r\nI (101) video: PC_STREAM_READY:5\r\ntail";
    for (std::size_t piece = 1; piece <= stream.size(); ++piece)
    {
        stream::BridgeDecoder decoder;
        auto result = decode(decoder, stream, piece);
        CHECK(result.records.size() == 2);
        CHECK(result.text == text);
        CHECK(decoder.corrupt() == 0);
    }
}

void recordInsideALogLine()
{
    const std::string stream = "I (7) video: sent" + record(BRIDGE_FRAME_DONE, 9) + "=3\r\n";
    stream::BridgeDecoder decoder;
    const auto result = decode(decoder, stream, 4);
    CHECK(result.records.size() == 1 && result.records[0].sequence == 9);
    CHECK(result.text == "I (7) video: sent=3\r\n");
}

void textThatLooksLikeAMagic()
{
    const std::string text = "Test TVB TV TVB2 TVB1 T\nTVB1\x7f end\n" + std::string(40, '.');
    stream::BridgeDecoder decoder;
    const auto result = decode(decoder, text, text.size());
    CHECK(result.records.empty());
    CHECK(result.text == text);
}

void damagedRecordDoesNotCostTheNext()
{
    std::string damaged = record(BRIDGE_ERROR, 0, "broken");
    damaged[RECORD_HEADER_SIZE + 2] ^= 0x40; // payload no longer matches its checksum
    const std::string stream = damaged + record(BRIDGE_FRAME_DONE, 7);
    stream::BridgeDecoder decoder;
    const auto result = decode(decoder, stream, 5);
    CHECK(decoder.corrupt() == 1);
    CHECK(result.records.size() == 1 && result.records[0].sequence == 7);
}

void truncatedRecordDoesNotCostTheNext()
{
    // A record cut short is followed by whole ones; the decoder must find them.
    const std::string whole = record(BRIDGE_INPUT, 3, std::string(128, 'x'));
    const std::string stream = whole.substr(0, 60) + record(BRIDGE_FRAME_DONE, 11) +
                               record(BRIDGE_FRAME_DONE, 12) + std::string(300, ' ') +
                               record(BRIDGE_FRAME_DONE, 13);
    stream::BridgeDecoder decoder;
    const auto result = decode(decoder, stream, 17);
    std::vector<std::uint32_t> sequences;
    for (const auto& r : result.records)
        sequences.push_back(r.sequence);
    CHECK((sequences == std::vector<std::uint32_t>{11, 12, 13}));
}

void impossibleHeadersAreText()
{
    std::string tooLong = record(BRIDGE_ERROR, 0, "x");
    tooLong[12] = char(0xff); // length 255 + 1: with the checksum wrong it can never be valid
    tooLong[13] = 0x7f;       // 0x7fff: over BRIDGE_MAX_PAYLOAD
    std::string padded = record(BRIDGE_ERROR, 0, "x");
    padded[6] = 1; // the three bytes after the type must be zero
    std::string downstream = std::string("TVU1") + record(BRIDGE_ERROR, 0).substr(4);
    stream::BridgeDecoder decoder;
    const auto result = decode(decoder, tooLong + padded + downstream + record(BRIDGE_IDR_REQUEST, 0), 64);
    CHECK(result.records.size() == 1 && result.records[0].type == BRIDGE_IDR_REQUEST);
}

// The firmware parser, fed what the PC's stream::packet() produces (same encoder, TVU1 magic).
struct Received {
    std::vector<std::uint8_t> types;
    std::vector<std::uint32_t> crcs;
    std::vector<std::uint8_t> buffer = std::vector<std::uint8_t>(MAX_USB_PAYLOAD);
};

std::uint8_t* reserve(void* context, std::uint8_t type, std::uint32_t, std::uint32_t)
{
    static_cast<Received*>(context)->types.push_back(type);
    return static_cast<Received*>(context)->buffer.data();
}

void finish(void* context, std::uint32_t crc) { static_cast<Received*>(context)->crcs.push_back(crc); }

std::string downstream(std::uint8_t type, std::uint32_t sequence, const std::string& payload)
{
    std::vector<std::uint8_t> out(RECORD_HEADER_SIZE + payload.size());
    record_encode(out.data(), "TVU1", type, sequence, payload.data(), payload.size());
    return std::string(out.begin(), out.end());
}

void firmwareParser()
{
    Received received;
    record_parser_t parser{};
    parser.context = &received;
    parser.reserve = reserve;
    parser.finish = finish;
    const std::string four("\x10\x00\x00\x00", 4);
    const std::string pair("PAIR\x1b\x00\x00\x00", 8); // magic, then the pairing code
    const std::string stream = downstream(STREAM_END, 1, "") + "noise" + downstream(VIBRATE, 2, four) +
                               downstream(99, 3, "") + downstream(VIBRATE, 4, "bad") +
                               downstream(PAIR_RESET, 5, four) + downstream(PAIR_RESET, 5, pair) +
                               downstream(STREAM_END, 6, "");
    for (char byte : stream)
        record_feed(&parser, reinterpret_cast<const std::uint8_t*>(&byte), 1);
    const std::string cut = "TVU1c" + downstream(STREAM_END, 7, "");
    for (char byte : cut)
        record_feed(&parser, reinterpret_cast<const std::uint8_t*>(&byte), 1);
    const std::vector<std::uint8_t> expected{STREAM_END, VIBRATE, PAIR_RESET, STREAM_END, STREAM_END};
    CHECK(received.types == expected);
    const std::uint32_t payloadCrc = toffee_crc32(four.data(), four.size());
    const std::uint32_t pairCrc = toffee_crc32(pair.data(), pair.size());
    const std::vector<std::uint32_t> expectedCrcs{0, payloadCrc, pairCrc, 0, 0};
    CHECK(received.crcs == expectedCrcs);
}

void pictureRing()
{
    picture_ring_t ring;
    picture_ring_reset(&ring, 100);
    CHECK(picture_ring_place(&ring, 0) == -1);   // nothing to place
    CHECK(picture_ring_place(&ring, 101) == -1); // larger than the buffer
    CHECK(picture_ring_place(&ring, 40) == 0);
    CHECK(picture_ring_place(&ring, 40) == 40);
    CHECK(picture_ring_place(&ring, 40) == -1); // 20 bytes left
    CHECK(picture_ring_free(&ring, 0));         // the oldest goes first
    CHECK(picture_ring_place(&ring, 40) == 0);  // and its room is reused by wrapping
    CHECK(picture_ring_place(&ring, 30) == -1); // between the newest and the oldest: 0 bytes
    CHECK(!picture_ring_free(&ring, 123));      // not a placed picture
    CHECK(picture_ring_free(&ring, 0));         // the newest may be dropped mid-arrival
    CHECK(picture_ring_place(&ring, 40) == 0);
    CHECK(picture_ring_free(&ring, 40));
    CHECK(picture_ring_free(&ring, 0));
    CHECK(!picture_ring_free(&ring, 0)); // empty

    picture_ring_reset(&ring, 1000);
    for (int i = 0; i < PICTURE_RING_ITEMS; ++i)
        CHECK(picture_ring_place(&ring, 10) == long(10 * i));
    CHECK(picture_ring_place(&ring, 1) == -1);
}
} // namespace

int main()
{
    crc();
    roundTrip();
    textAndRecordsInterleaved();
    recordInsideALogLine();
    textThatLooksLikeAMagic();
    damagedRecordDoesNotCostTheNext();
    truncatedRecordDoesNotCostTheNext();
    impossibleHeadersAreText();
    firmwareParser();
    pictureRing();
    if (test::failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", test::failures);
        return 1;
    }
    std::puts("all protocol tests passed");
    return 0;
}
