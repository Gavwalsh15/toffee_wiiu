#pragma once

#include "wire_limits.h"

#define VIDEO_WIDTH 864u
#define VIDEO_HEIGHT 480u
#define VIDEO_FPS 60u
#define VIDEO_FRAME_PERIOD_US 16683u
#define VIDEO_STREAM_PROFILE 5u

#ifdef __cplusplus
#include <cstdint>
namespace toffee::video {
inline constexpr std::uint32_t WIDTH = VIDEO_WIDTH;
inline constexpr std::uint32_t HEIGHT = VIDEO_HEIGHT;
inline constexpr std::uint32_t CHROMA_WIDTH = WIDTH / 2;
inline constexpr std::uint32_t CHROMA_HEIGHT = HEIGHT / 2;
inline constexpr std::uint32_t LUMA_BYTES = WIDTH * HEIGHT;
inline constexpr std::uint32_t CHROMA_BYTES = CHROMA_WIDTH * CHROMA_HEIGHT;
inline constexpr std::uint32_t U_OFFSET = LUMA_BYTES;
inline constexpr std::uint32_t V_OFFSET = U_OFFSET + CHROMA_BYTES;
inline constexpr std::uint32_t I420_BYTES = V_OFFSET + CHROMA_BYTES;
inline constexpr std::uint32_t FRAME_PERIOD_US = VIDEO_FRAME_PERIOD_US;
inline constexpr std::uint8_t STREAM_PROFILE = VIDEO_STREAM_PROFILE;
inline constexpr std::uint32_t CHUNK_COUNT = DRH_CHUNK_COUNT;
inline constexpr std::uint32_t MAX_RECORD_BYTES = MAX_USB_PAYLOAD;
inline constexpr int MAX_PICTURE_QP = 44;
} // namespace toffee::video
#endif
