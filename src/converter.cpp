#include "converter.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <limits>
#include <numeric>
#include <unordered_set>

namespace svg_app {
namespace {

using Contours = std::vector<std::vector<cv::Point>>;

constexpr std::array<int, 10> kAllowedColors{2, 3, 4, 5, 6, 8, 12, 16, 24, 32};

bool quantize(const cv::Mat& bgr, int colorCount, cv::Mat& labelImage,
              std::vector<cv::Vec3b>& palette, std::string& error) {
    cv::Mat bgrF;
    bgr.convertTo(bgrF, CV_32F, 1.0 / 255.0);
    cv::Mat lab;
    cv::cvtColor(bgrF, lab, cv::COLOR_BGR2Lab);

    if (lab.total() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        error = "Image is too large to process.";
        return false;
    }
    const cv::Mat samples = lab.reshape(1, static_cast<int>(lab.total()));
    if (samples.rows < colorCount) {
        error = std::format("Image is too small for {} colors.", colorCount);
        return false;
    }

    cv::setRNGSeed(12345);
    cv::Mat labels;
    cv::Mat centers;
    cv::kmeans(
        samples,
        colorCount,
        labels,
        cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.5),
        3,
        cv::KMEANS_PP_CENTERS,
        centers
    );

    labelImage = labels.reshape(1, bgr.rows).clone();

    cv::Mat centerLab = centers.reshape(3, 1);
    cv::Mat centerBgr;
    cv::cvtColor(centerLab, centerBgr, cv::COLOR_Lab2BGR);
    palette.resize(static_cast<std::size_t>(colorCount));
    for (int i = 0; i < colorCount; ++i) {
        const cv::Vec3f color = centerBgr.at<cv::Vec3f>(0, i);
        for (int channel = 0; channel < 3; ++channel) {
            palette[static_cast<std::size_t>(i)][channel] =
                cv::saturate_cast<uchar>(color[channel] * 255.0f);
        }
    }
    return true;
}

bool isImageFrame(const std::vector<cv::Point>& contour, int cols, int rows) {
    if (cols <= 1 || rows <= 1 ||
        cv::boundingRect(contour) != cv::Rect(0, 0, cols, rows)) {
        return false;
    }
    return std::all_of(contour.begin(), contour.end(), [&](const cv::Point& point) {
        return point.x == 0 || point.x == cols - 1 ||
               point.y == 0 || point.y == rows - 1;
    });
}

std::string contoursToPath(const Contours& contours, bool skipFrame,
                           int cols, int rows) {
    std::string path;
    for (const auto& contour : contours) {
        if (contour.empty() || (skipFrame && isImageFrame(contour, cols, rows))) {
            continue;
        }
        path += std::format("M{} {}", contour.front().x, contour.front().y);
        for (std::size_t i = 1; i < contour.size(); ++i) {
            path += std::format("L{} {}", contour[i].x, contour[i].y);
        }
        path += 'Z';
    }
    return path;
}

}  // namespace

std::string colorToHex(const cv::Vec3b& bgr) {
    return std::format("#{:02x}{:02x}{:02x}", bgr[2], bgr[1], bgr[0]);
}

bool analyzeImage(const cv::Mat& bgr, int colorCount, ImageAnalysis& analysis,
                  std::string& error) {
    analysis = {};
    error.clear();
    if (bgr.empty()) {
        error = "Image is empty.";
        return false;
    }
    if (bgr.type() != CV_8UC3) {
        error = "Image must be an 8-bit, 3-channel BGR image.";
        return false;
    }
    if (std::find(kAllowedColors.begin(), kAllowedColors.end(), colorCount) ==
        kAllowedColors.end()) {
        error = "Unsupported color count.";
        return false;
    }

    try {
        if (!quantize(bgr, colorCount, analysis.labels, analysis.palette, error)) {
            return false;
        }

        analysis.width = bgr.cols;
        analysis.height = bgr.rows;
        analysis.area.assign(analysis.palette.size(), 0);
        for (int y = 0; y < analysis.labels.rows; ++y) {
            for (int x = 0; x < analysis.labels.cols; ++x) {
                ++analysis.area[static_cast<std::size_t>(
                    analysis.labels.at<int>(y, x))];
            }
        }
        analysis.order.resize(analysis.palette.size());
        std::iota(analysis.order.begin(), analysis.order.end(), 0);
        std::stable_sort(analysis.order.begin(), analysis.order.end(),
                         [&](int first, int second) {
            return analysis.area[static_cast<std::size_t>(first)] >
                   analysis.area[static_cast<std::size_t>(second)];
        });
    } catch (const cv::Exception& exception) {
        analysis = {};
        error = exception.what();
        return false;
    }
    return true;
}

bool generateSvg(const ImageAnalysis& analysis, const SvgOptions& options,
                 SvgResult& result, std::string& error) {
    result = {};
    error.clear();
    if (analysis.labels.empty() || analysis.labels.type() != CV_32SC1 ||
        analysis.labels.cols != analysis.width ||
        analysis.labels.rows != analysis.height ||
        analysis.width <= 0 || analysis.height <= 0 ||
        analysis.palette.empty() || analysis.area.size() != analysis.palette.size() ||
        analysis.order.size() != analysis.palette.size()) {
        error = "Image analysis is incomplete or invalid.";
        return false;
    }

    std::vector<int> keptColors = options.keptColors;
    if (keptColors.empty()) {
        for (const int index : analysis.order) {
            if (index < 0 || static_cast<std::size_t>(index) >= analysis.palette.size()) {
                error = "Image analysis is incomplete or invalid.";
                return false;
            }
            if (analysis.area[static_cast<std::size_t>(index)] > 0) {
                keptColors.push_back(index);
            }
        }
    }

    std::unordered_set<int> uniqueColors;
    for (const int index : keptColors) {
        if (index < 0 || static_cast<std::size_t>(index) >= analysis.palette.size() ||
            analysis.area[static_cast<std::size_t>(index)] == 0 ||
            !uniqueColors.insert(index).second) {
            error = "Selected colors are invalid.";
            return false;
        }
    }

    try {
        result.svg =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<svg xmlns=\"http://www.w3.org/2000/svg\" "
            "width=\"" + std::to_string(analysis.width) +
            "\" height=\"" + std::to_string(analysis.height) +
            "\" viewBox=\"0 0 " + std::to_string(analysis.width) + " " +
            std::to_string(analysis.height) + "\">\n";

        for (const int index : keptColors) {
            const std::string color =
                colorToHex(analysis.palette[static_cast<std::size_t>(index)]);
            const cv::Mat mask = analysis.labels == index;
            Contours contours;
            cv::findContours(mask, contours, cv::RETR_CCOMP, cv::CHAIN_APPROX_SIMPLE);
            result.contourCount += contours.size();

            const std::string path =
                contoursToPath(contours, options.boundary, analysis.width, analysis.height);
            if (path.empty()) {
                continue;
            }
            if (options.boundary) {
                result.svg += "  <path fill=\"none\" stroke=\"" + color +
                              "\" stroke-width=\"1\" d=\"" + path + "\"/>\n";
            } else {
                result.svg += "  <path fill=\"" + color +
                              "\" fill-rule=\"evenodd\" stroke=\"" + color +
                              "\" stroke-width=\"1\" stroke-linejoin=\"round\" d=\"" +
                              path + "\"/>\n";
            }
        }
        result.svg += "</svg>\n";
    } catch (const cv::Exception& exception) {
        result = {};
        error = exception.what();
        return false;
    }
    return true;
}

}  // namespace svg_app
