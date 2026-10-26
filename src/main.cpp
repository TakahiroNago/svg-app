#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <format>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Contours = std::vector<std::vector<cv::Point>>;

constexpr std::array<int, 10> kAllowedColors{2, 3, 4, 5, 6, 8, 12, 16, 24, 32};

struct Options {
    std::string input;
    std::string output;
    int colors = 2;
    bool boundary = false;
};

void printUsage() {
    std::cerr << "Usage: svg-app <input-image> <output.svg> "
                 "[--colors 2|3|4|5|6|8|12|16|24|32] [--boundary]\n";
}

bool parseArgs(int argc, char* argv[], Options& opt) {
    std::vector<std::string_view> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--boundary") {
            opt.boundary = true;
        } else if (arg == "--colors") {
            if (i + 1 >= argc) {
                return false;
            }
            const std::string_view value(argv[++i]);
            int n = 0;
            try {
                n = std::stoi(std::string(value));
            } catch (...) {
                return false;
            }
            if (std::find(kAllowedColors.begin(), kAllowedColors.end(), n) ==
                kAllowedColors.end()) {
                return false;
            }
            opt.colors = n;
        } else if (arg.starts_with("--")) {
            return false;
        } else {
            positional.push_back(arg);
        }
    }
    if (positional.size() != 2) {
        return false;
    }
    opt.input = std::string(positional[0]);
    opt.output = std::string(positional[1]);
    return true;
}

// k-means in Lab space. Returns a label image (CV_32S) and BGR palette (0-255).
bool quantize(const cv::Mat& bgr, int k, cv::Mat& labelImage,
              std::vector<cv::Vec3b>& palette) {
    cv::Mat bgrF;
    bgr.convertTo(bgrF, CV_32F, 1.0 / 255.0);
    cv::Mat lab;
    cv::cvtColor(bgrF, lab, cv::COLOR_BGR2Lab);

    const cv::Mat samples = lab.reshape(1, static_cast<int>(lab.total()));
    if (samples.rows < k) {
        return false;
    }

    cv::setRNGSeed(12345);
    cv::Mat labels;
    cv::Mat centers;
    cv::kmeans(
        samples,
        k,
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
    palette.resize(static_cast<std::size_t>(k));
    for (int i = 0; i < k; ++i) {
        const cv::Vec3f c = centerBgr.at<cv::Vec3f>(0, i);
        for (int ch = 0; ch < 3; ++ch) {
            palette[static_cast<std::size_t>(i)][ch] =
                cv::saturate_cast<uchar>(c[ch] * 255.0f);
        }
    }
    return true;
}

std::string toHex(const cv::Vec3b& bgr) {
    return std::format("#{:02x}{:02x}{:02x}", bgr[2], bgr[1], bgr[0]);
}

bool isImageFrame(const std::vector<cv::Point>& contour, int cols, int rows) {
    if (cols <= 1 || rows <= 1 ||
        cv::boundingRect(contour) != cv::Rect(0, 0, cols, rows)) {
        return false;
    }
    return std::all_of(contour.begin(), contour.end(), [&](const cv::Point& p) {
        return p.x == 0 || p.x == cols - 1 || p.y == 0 || p.y == rows - 1;
    });
}

std::string contoursToPath(const Contours& contours, bool skipFrame,
                           int cols, int rows) {
    std::string d;
    for (const auto& contour : contours) {
        if (contour.empty()) {
            continue;
        }
        if (skipFrame && isImageFrame(contour, cols, rows)) {
            continue;
        }
        d += std::format("M{} {}", contour.front().x, contour.front().y);
        for (std::size_t i = 1; i < contour.size(); ++i) {
            d += std::format("L{} {}", contour[i].x, contour[i].y);
        }
        d += 'Z';
    }
    return d;
}

// Lists the palette (largest area first, 1-based) and asks which colors to keep.
// Empty input keeps all colors. Returns palette indices in drawing order.
std::vector<int> askColorsToKeep(const std::vector<int>& order,
                                 const std::vector<int>& area,
                                 const std::vector<cv::Vec3b>& palette) {
    std::vector<int> present;
    for (const int k : order) {
        if (area[static_cast<std::size_t>(k)] > 0) {
            present.push_back(k);
        }
    }

    std::cout << "Palette:\n";
    for (std::size_t i = 0; i < present.size(); ++i) {
        const auto k = static_cast<std::size_t>(present[i]);
        std::cout << std::format("  {:>2}: {}  ({} px)\n", i + 1,
                                 toHex(palette[k]), area[k]);
    }

    while (true) {
        std::cout << "Colors to keep (numbers separated by space/comma, "
                     "empty = all): " << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) {
            return present;
        }
        for (char& c : line) {
            if (c == ',') {
                c = ' ';
            }
        }
        std::istringstream in(line);
        std::vector<bool> selected(present.size(), false);
        bool any = false;
        bool valid = true;
        std::string token;
        while (in >> token) {
            int n = 0;
            try {
                std::size_t pos = 0;
                n = std::stoi(token, &pos);
                valid = pos == token.size();
            } catch (...) {
                valid = false;
            }
            if (!valid || n < 1 || n > static_cast<int>(present.size())) {
                valid = false;
                break;
            }
            selected[static_cast<std::size_t>(n - 1)] = true;
            any = true;
        }
        if (!valid) {
            std::cout << "Invalid input. Enter numbers from 1 to "
                      << present.size() << ".\n";
            continue;
        }
        if (!any) {
            return present;
        }
        std::vector<int> kept;
        for (std::size_t i = 0; i < present.size(); ++i) {
            if (selected[i]) {
                kept.push_back(present[i]);
            }
        }
        return kept;
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    Options opt;
    if (!parseArgs(argc, argv, opt)) {
        printUsage();
        return 1;
    }

    cv::Mat image = cv::imread(opt.input, cv::IMREAD_COLOR);
    if (image.empty()) {
        std::cerr << "Error: Failed to load image.\n";
        return 1;
    }

    cv::Mat labelImage;
    std::vector<cv::Vec3b> palette;
    if (!quantize(image, opt.colors, labelImage, palette)) {
        std::cerr << "Error: Image is too small for " << opt.colors
                  << " colors.\n";
        return 1;
    }

    // Draw larger regions first so smaller ones sit on top.
    std::vector<int> area(palette.size(), 0);
    for (int y = 0; y < labelImage.rows; ++y) {
        for (int x = 0; x < labelImage.cols; ++x) {
            ++area[static_cast<std::size_t>(labelImage.at<int>(y, x))];
        }
    }
    std::vector<int> order(palette.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return area[static_cast<std::size_t>(a)] > area[static_cast<std::size_t>(b)];
    });

    const std::vector<int> kept = askColorsToKeep(order, area, palette);

    std::ofstream svg(opt.output);
    if (!svg) {
        std::cerr << "Error: Failed to open output file: " << opt.output << '\n';
        return 1;
    }

    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        << "width=\"" << image.cols << "\" "
        << "height=\"" << image.rows << "\" "
        << "viewBox=\"0 0 " << image.cols << ' ' << image.rows << "\">\n";

    std::size_t totalContours = 0;
    for (const int k : kept) {
        const std::string color = toHex(palette[static_cast<std::size_t>(k)]);

        const cv::Mat mask = labelImage == k;
        Contours contours;
        cv::findContours(mask, contours, cv::RETR_CCOMP, cv::CHAIN_APPROX_SIMPLE);
        totalContours += contours.size();

        const std::string d =
            contoursToPath(contours, opt.boundary, image.cols, image.rows);
        if (d.empty()) {
            continue;
        }
        if (opt.boundary) {
            svg << "  <path fill=\"none\" stroke=\"" << color
                << "\" stroke-width=\"1\" d=\"" << d << "\"/>\n";
        } else {
            // Same-colour stroke covers the half-pixel gaps between regions.
            svg << "  <path fill=\"" << color << "\" fill-rule=\"evenodd\" stroke=\""
                << color << "\" stroke-width=\"1\" stroke-linejoin=\"round\" d=\""
                << d << "\"/>\n";
        }
    }

    svg << "</svg>\n";
    svg.close();
    if (!svg) {
        std::cerr << "Error: Failed to write output file: " << opt.output << '\n';
        return 1;
    }

    std::cout << "OpenCV version: " << CV_VERSION << '\n'
              << "Image size: " << image.cols << " x " << image.rows << '\n'
              << "Colors: " << opt.colors << '\n'
              << "Contours found: " << totalContours << '\n'
              << "SVG saved: " << opt.output << '\n';
    return 0;
}
