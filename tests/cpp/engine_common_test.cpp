// Dependency-free unit tests for the shared engine code. Run through CTest.
#include "converter.hpp"
#include "engine_common.hpp"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAILED line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

void testDimensions() {
    std::string error;
    CHECK(svg_app::validateDimensions(1, 1, error));
    CHECK(!svg_app::validateDimensions(0, 10, error));
    CHECK(error == "Image width and height must be positive.");
    CHECK(!svg_app::validateDimensions(10, -1, error));
    CHECK(!svg_app::validateDimensions(svg_app::kMaxDimension + 1, 1, error));
    CHECK(error == "Image is too large.");
    CHECK(!svg_app::validateDimensions(8193, 8193, error));  // over kMaxPixels
}

void testRgbaLength() {
    std::string error;
    CHECK(svg_app::validateRgbaLength(2 * 3 * 4, 2, 3, error));
    CHECK(!svg_app::validateRgbaLength(2 * 3 * 4 - 1, 2, 3, error));
    CHECK(error.find("expected 24 bytes") != std::string::npos);
}

void testRgbaToBgrOnWhite() {
    // Opaque red, opaque blue, 50% red, fully transparent.
    const std::vector<std::uint8_t> rgba = {
        255, 0, 0, 255,
        0, 0, 255, 255,
        255, 0, 0, 128,
        12, 34, 56, 0,
    };
    const cv::Mat bgr = svg_app::rgbaToBgrOnWhite(rgba.data(), 4, 1);
    CHECK(bgr.type() == CV_8UC3 && bgr.cols == 4 && bgr.rows == 1);
    CHECK(bgr.at<cv::Vec3b>(0, 0) == cv::Vec3b(0, 0, 255));      // red stays red
    CHECK(bgr.at<cv::Vec3b>(0, 1) == cv::Vec3b(255, 0, 0));      // blue stays blue
    CHECK(bgr.at<cv::Vec3b>(0, 2) == cv::Vec3b(127, 127, 255));  // #ff7f7f over white
    CHECK(bgr.at<cv::Vec3b>(0, 3) == cv::Vec3b(255, 255, 255));  // transparent -> white
}

void testConversionIsDeterministic() {
    const int w = 8, h = 4;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w * h * 4));
    for (int i = 0; i < w * h; ++i) {
        const bool left = i % w < w / 2;
        const std::uint8_t px[4] = {static_cast<std::uint8_t>(left ? 255 : 0), 0,
                                    static_cast<std::uint8_t>(left ? 0 : 255), 255};
        std::copy(px, px + 4, rgba.begin() + i * 4);
    }
    const cv::Mat bgr = svg_app::rgbaToBgrOnWhite(rgba.data(), w, h);

    svg_app::SvgResult first, second;
    std::string error;
    for (svg_app::SvgResult* result : {&first, &second}) {
        svg_app::ImageAnalysis analysis;
        CHECK(svg_app::analyzeImage(bgr, 2, analysis, error));
        CHECK(analysis.palette.size() == 2);
        CHECK(svg_app::generateSvg(analysis, {}, *result, error));
    }
    CHECK(first.svg == second.svg);
    CHECK(first.svg.find("#ff0000") != std::string::npos);
    CHECK(first.svg.find("#0000ff") != std::string::npos);

    svg_app::ImageAnalysis analysis;
    svg_app::SvgResult bad;
    CHECK(!svg_app::analyzeImage(bgr, 7, analysis, error));  // unsupported color count
    CHECK(svg_app::analyzeImage(bgr, 2, analysis, error));
    CHECK(!svg_app::generateSvg(analysis, {false, {99}}, bad, error));
}

}  // namespace

int main() {
    testDimensions();
    testRgbaLength();
    testRgbaToBgrOnWhite();
    testConversionIsDeterministic();
    if (failures == 0) {
        std::cout << "All engine tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
