#pragma once

#include <opencv2/core.hpp>

#include <cstdint>
#include <string>

// Input handling shared by the WASM binding and the native gRPC server so both
// engines accept exactly the same pixels and produce the same BGR image.
namespace svg_app {

inline constexpr std::uint64_t kMaxPixels = 64ULL * 1024 * 1024 / 4;
inline constexpr int kMaxDimension = 16384;

// Rejects non-positive or oversized dimensions.
bool validateDimensions(int width, int height, std::string& error);

// Checks that an RGBA buffer holds exactly width * height * 4 bytes.
bool validateRgbaLength(std::uint64_t byteLength, int width, int height,
                        std::string& error);

// Straight (non-premultiplied) RGBA composited over white into an 8-bit BGR image.
cv::Mat rgbaToBgrOnWhite(const std::uint8_t* rgba, int width, int height);

}  // namespace svg_app
