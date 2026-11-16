#include "drh_encoder.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <fcntl.h>
#include <io.h>

int main()
{
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    try
    {
        const char* trace_path = std::getenv("TOFFEE_ENCODER_PROFILE");
        std::unique_ptr<FILE, decltype(&std::fclose)> trace(
            trace_path && *trace_path ? std::fopen(trace_path, "w") : nullptr, &std::fclose);
        if (trace_path && *trace_path && !trace)
            throw std::runtime_error("Cannot open encoder timing trace");
        if (trace)
            std::fprintf(
                trace.get(),
                "seq,idr,bytes,read_begin_us,read_done_us,encode_done_us,write_done_us,x264_us\n");
        const auto now = [] {
            return std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        };
        unsigned sequence = 0;
        const char* preset = std::getenv("TOFFEE_ENCODER_PRESET");
        if (!preset || !*preset)
            preset = ToffeeEncoder::DefaultPreset;
        const std::string selected(preset);
        if (selected != "medium" && selected != "fast" && selected != "faster" &&
            selected != "veryfast" && selected != "superfast")
            throw std::runtime_error("Unsupported encoder preset");
        int pQp = 32;
        if (const char* qp = std::getenv("TOFFEE_ENCODER_QP"); qp && *qp)
        {
            const std::string value(qp);
            size_t used = 0;
            pQp = std::stoi(value, &used);
            if (used != value.size() || pQp < 8 || pQp > 40) // Move to ToffeeEncoder::MAX_PICTURE_QP
                throw std::runtime_error("Encoder QP must be 8..40");
        }
        const int idrQp = std::max(36, pQp + 4);
        ToffeeEncoder encoder(preset, std::exp2((pQp - idrQp) / 6.0f), pQp);
        std::vector<uint8_t> pixels(toffee::video::I420_BYTES);
        while (true)
        {
            const auto read_begin = trace ? now() : 0;
            int flag = std::getchar();
            if (flag == EOF)
                return 0;
            const int pictureQp = flag >> 1;
            if (pictureQp && (pictureQp < pQp || pictureQp > toffee::video::MAX_PICTURE_QP))
                throw std::runtime_error("Picture QP out of range");
            if (std::fread(pixels.data(), 1, pixels.size(), stdin) != pixels.size())
                throw std::runtime_error("Truncated live input");
            const auto read_done = trace ? now() : 0;
            ToffeeEncoder::Metrics metrics;
            auto record = encoder.encode(pixels, flag & 1, pictureQp, trace ? &metrics : nullptr);
            const auto encode_done = trace ? now() : 0;
            uint8_t size[4];
            for (unsigned s = 0; s < 4; ++s)
                size[s] = uint8_t(record.size() >> (8 * s));
            if (std::fwrite(size, 1, 4, stdout) != 4 ||
                std::fwrite(record.data(), 1, record.size(), stdout) != record.size() ||
                std::fflush(stdout))
                throw std::runtime_error("Live output write failed");
            if (trace)
                std::fprintf(trace.get(), "%u,%d,%zu,%lld,%lld,%lld,%lld,%lld\n", sequence++, flag & 1,
                             record.size(), (long long)read_begin, (long long)read_done,
                             (long long)encode_done, (long long)now(), (long long)metrics.x264_us);
        }
    } catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
