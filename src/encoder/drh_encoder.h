// Confirmed GamePad encoder configuration, adapted from Strawberry's DRH sender.
#pragma once
#include <cstdint>
#include "common/video_profile.h"
extern "C" {
#include "x264.h"
}
#include <array>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

class ToffeeEncoder {
    struct Frame {
        std::array<const uint8_t*, DRH_CHUNK_COUNT> data{};
        std::array<size_t, DRH_CHUNK_COUNT> size{};
        unsigned count = 0;
        bool idr = false;
    };
    x264_t* encoder;
    int64_t number = 0;
    static void collect(x264_t*, x264_nal_t* nal, void* opaque)
    {
        if (nal->i_type == NAL_SEI)
            return;
        auto& frame = *static_cast<Frame*>(opaque);
        const unsigned index = unsigned(nal->i_first_mb) / 324;
        if (index >= DRH_CHUNK_COUNT || frame.data[index])
        {
            frame.count = 100;
            return;
        }
        frame.data[index] = nal->p_payload;
        frame.size[index] = nal->i_payload;
        ++frame.count;
        frame.idr = nal->i_type == NAL_SLICE_IDR;
    }

  public:
    static constexpr const char* DefaultPreset = "veryfast";
    struct Metrics {
        int64_t x264_us = 0;
    };
    explicit ToffeeEncoder(const char* preset = DefaultPreset, float ip_factor = 1.0f,
                           int p_qp = 32)
    {
        x264_param_t p;
        if (x264_param_default_preset(&p, preset, "zerolatency") < 0)
            throw std::runtime_error("Invalid encoder preset");
        p.i_width = toffee::video::WIDTH;
        p.i_height = toffee::video::HEIGHT;
        p.i_csp = X264_CSP_I420;
        p.analyse.inter &= ~X264_ANALYSE_PSUB16x16;
        p.i_keyint_max = X264_KEYINT_MAX_INFINITE;
        p.i_scenecut_threshold = -1;
        p.b_cabac = 1;
        p.b_interlaced = 0;
        p.i_bframe = p.i_bframe_pyramid = 0;
        p.i_frame_reference = 1;
        p.b_constrained_intra = 1;
        p.i_level_idc = 9;
        p.analyse.i_weighted_pred = p.analyse.b_weighted_bipred = 0;
        p.analyse.b_transform_8x8 = p.analyse.i_chroma_qp_offset = 0;
        p.rc.i_rc_method = X264_RC_CQP;
        p.rc.i_qp_constant = p.rc.i_qp_min = p.rc.i_qp_max = p_qp;
        p.rc.f_ip_factor = ip_factor;
        p.rc.f_pb_factor = std::exp2((toffee::video::MAX_PICTURE_QP - p_qp) / 6.0f);
        p.b_repeat_headers = p.b_aud = 0;
        p.b_drh_mode = 1;
        p.i_threads = 1;
        p.b_sliced_threads = 0;
        p.i_slice_count = 1;
        p.nalu_process = collect;
        p.i_log_level = X264_LOG_WARNING;
        x264_param_apply_profile(&p, "main");
        encoder = x264_encoder_open(&p);
        if (!encoder)
            throw std::runtime_error("drc-x264 initialization failed");
    }
    ~ToffeeEncoder() { x264_encoder_close(encoder); }
    ToffeeEncoder(const ToffeeEncoder&) = delete;
    ToffeeEncoder& operator=(const ToffeeEncoder&) = delete;
    std::vector<uint8_t> encode(std::vector<uint8_t>& pixels, bool idr, int qp = 0,
                                Metrics* metrics = nullptr)
    {
        if (pixels.size() != toffee::video::I420_BYTES)
            throw std::runtime_error("Expected one complete 864x480 I420 picture");
        Frame frame;
        x264_picture_t input, output;
        x264_picture_init(&input);
        input.opaque = &frame;
        input.img.i_csp = X264_CSP_I420;
        input.img.i_plane = 3;
        input.img.i_stride[0] = toffee::video::WIDTH;
        input.img.i_stride[1] = input.img.i_stride[2] = toffee::video::CHROMA_WIDTH;
        input.img.plane[0] = pixels.data();
        input.img.plane[1] = pixels.data() + toffee::video::U_OFFSET;
        input.img.plane[2] = pixels.data() + toffee::video::V_OFFSET;
        input.i_type = idr ? X264_TYPE_IDR : X264_TYPE_P;
        if (qp > 0 && !idr)
            input.i_qpplus1 = qp + 1;
        input.i_pts = number++;
        x264_nal_t* nals;
        int count;
        const auto started =
            metrics ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        const int result = x264_encoder_encode(encoder, &nals, &count, &input, &output);
        if (metrics)
            metrics->x264_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::steady_clock::now() - started)
                                   .count();
        if (result < 0 || frame.count != DRH_CHUNK_COUNT || frame.idr != idr)
            throw std::runtime_error("DRH encoder did not produce five matching chunks");
        std::vector<uint8_t> record;
        auto u32 = [&](uint32_t value) {
            for (unsigned s = 0; s < 32; s += 8)
                record.push_back(uint8_t(value >> s));
        };
        size_t total = DRH_RECORD_OVERHEAD;
        bool oversized = false;
        for (unsigned c = 0; c < DRH_CHUNK_COUNT; ++c)
        {
            total += frame.size[c];
            oversized |= frame.size[c] > MAX_CHUNK_SIZE;
        }
        // A sudden full-screen change at a low QP can outgrow a record
        if (!idr && (oversized || total > MAX_USB_PAYLOAD))
            return encode(pixels, true, 0, metrics);
        record.push_back(frame.idr);
        u32(DRH_CHUNK_COUNT);
        // Callback views remain mutable during CABAC carry.  copy after Encode.
        for (unsigned c = 0; c < DRH_CHUNK_COUNT; ++c)
        {
            if (!frame.size[c] || frame.size[c] > MAX_CHUNK_SIZE)
                throw std::runtime_error(
                    "Encoded chunk exceeds bounded USB frame capacity (64 KiB)");
            u32(frame.size[c]);
            record.insert(record.end(), frame.data[c], frame.data[c] + frame.size[c]);
        }
        if (record.size() > MAX_USB_PAYLOAD)
            throw std::runtime_error("Encoded picture exceeds USB frame capacity (64 KiB): " +
                                     std::to_string(record.size()) + " bytes");
        return record;
    }
};
