#include "engine_common.hpp"

#include <format>

namespace svg_app {

bool validateDimensions(int width, int height, std::string& error) {
    if (width <= 0 || height <= 0) {
        error = "Image width and height must be positive.";
        return false;
    }
    if (width > kMaxDimension || height > kMaxDimension ||
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > kMaxPixels) {
        error = "Image is too large.";
        return false;
    }
    return true;
}

bool validateRgbaLength(std::uint64_t byteLength, int width, int height,
                        std::string& error) {
    const std::uint64_t expected =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4;
    if (byteLength != expected) {
        error = std::format("Pixel data size mismatch: expected {} bytes (RGBA {}x{}), got {}.",
                            expected, width, height, byteLength);
        return false;
    }
    return true;
}

// out = (c * a + 255 * (255 - a)) / 255, rounded to nearest.
cv::Mat rgbaToBgrOnWhite(const std::uint8_t* rgba, int width, int height) {
    cv::Mat bgr(height, width, CV_8UC3);
    const std::uint8_t* src = rgba;
    for (int y = 0; y < height; ++y) {
        auto* dst = bgr.ptr<std::uint8_t>(y);
        for (int x = 0; x < width; ++x, src += 4, dst += 3) {
            const unsigned a = src[3];
            const unsigned inv = 255 - a;
            dst[0] = static_cast<std::uint8_t>((src[2] * a + 255 * inv + 127) / 255);
            dst[1] = static_cast<std::uint8_t>((src[1] * a + 255 * inv + 127) / 255);
            dst[2] = static_cast<std::uint8_t>((src[0] * a + 255 * inv + 127) / 255);
        }
    }
    return bgr;
}

}  // namespace svg_app
