#include "converter.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <format>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

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

// Lists the palette (largest area first, 1-based) and asks which colors to keep.
// Empty input keeps all colors. Returns palette indices in drawing order.
std::vector<int> askColorsToKeep(const svg_app::ImageAnalysis& analysis) {
    std::vector<int> present;
    for (const int index : analysis.order) {
        if (analysis.area[static_cast<std::size_t>(index)] > 0) {
            present.push_back(index);
        }
    }

    std::cout << "Palette:\n";
    for (std::size_t i = 0; i < present.size(); ++i) {
        const auto index = static_cast<std::size_t>(present[i]);
        std::cout << std::format("  {:>2}: {}  ({} px)\n", i + 1,
                                 svg_app::colorToHex(analysis.palette[index]),
                                 analysis.area[index]);
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

    svg_app::ImageAnalysis analysis;
    std::string error;
    if (!svg_app::analyzeImage(image, opt.colors, analysis, error)) {
        std::cerr << "Error: " << error << '\n';
        return 1;
    }

    const std::vector<int> kept = askColorsToKeep(analysis);

    svg_app::SvgResult result;
    if (!svg_app::generateSvg(analysis, {opt.boundary, kept}, result, error)) {
        std::cerr << "Error: " << error << '\n';
        return 1;
    }

    std::ofstream output(opt.output);
    if (!output) {
        std::cerr << "Error: Failed to open output file: " << opt.output << '\n';
        return 1;
    }
    output << result.svg;
    output.close();
    if (!output) {
        std::cerr << "Error: Failed to write output file: " << opt.output << '\n';
        return 1;
    }

    std::cout << "OpenCV version: " << CV_VERSION << '\n'
              << "Image size: " << analysis.width << " x " << analysis.height << '\n'
              << "Colors: " << opt.colors << '\n'
              << "Contours found: " << result.contourCount << '\n'
              << "SVG saved: " << opt.output << '\n';
    return 0;
}
