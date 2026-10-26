#pragma once

#include <opencv2/core.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace svg_app {

struct ImageAnalysis {
    cv::Mat labels;
    std::vector<cv::Vec3b> palette;
    std::vector<std::size_t> area;
    std::vector<int> order;
    int width = 0;
    int height = 0;
};

struct SvgOptions {
    bool boundary = false;
    std::vector<int> keptColors;
};

struct SvgResult {
    std::string svg;
    std::size_t contourCount = 0;
};

bool analyzeImage(const cv::Mat& bgr, int colorCount, ImageAnalysis& analysis,
                  std::string& error);
bool generateSvg(const ImageAnalysis& analysis, const SvgOptions& options,
                 SvgResult& result, std::string& error);
std::string colorToHex(const cv::Vec3b& bgr);

}  // namespace svg_app
