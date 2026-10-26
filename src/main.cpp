
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <fstream>
#include <iostream>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: svg-app <input-image> <output.svg>\n";
        return 1;
    }

    cv::Mat image = cv::imread(
        argv[1],
        cv::IMREAD_GRAYSCALE
    );

    if (image.empty()) {
        std::cerr << "Error: Failed to load image.\n";
        return 1;
    }

    cv::Mat binary;

    cv::threshold(
        image,
        binary,
        0,
        255,
        cv::THRESH_BINARY_INV | cv::THRESH_OTSU
    );

    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;

    cv::findContours(
        binary,
        contours,
        cv::RETR_TREE,
        cv::CHAIN_APPROX_SIMPLE
    );

    std::ofstream svg(argv[2]);
    if (!svg) {
        std::cerr << "Error: Failed to open output file: "
                  << argv[2] << '\n';
        return 1;
    }

    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        << "width=\"" << image.cols << "\" "
        << "height=\"" << image.rows << "\" "
        << "viewBox=\"0 0 " << image.cols << ' ' << image.rows << "\">\n"
        << "  <path fill=\"black\" fill-rule=\"evenodd\" d=\"";

    for (const auto& contour : contours) {
        if (contour.empty()) {
            continue;
        }

        svg << 'M' << contour.front().x << ' ' << contour.front().y;
        for (std::size_t i = 1; i < contour.size(); ++i) {
            svg << 'L' << contour[i].x << ' ' << contour[i].y;
        }
        svg << 'Z';
    }

    svg << "\"/>\n</svg>\n";
    svg.close();
    if (!svg) {
        std::cerr << "Error: Failed to write output file: "
                  << argv[2] << '\n';
        return 1;
    }

    std::cout << "OpenCV version: "
              << CV_VERSION << '\n';

    std::cout << "Image size: "
              << image.cols << " x "
              << image.rows << '\n';

    std::cout << "Contours found: "
              << contours.size() << '\n';

    std::cout << "SVG saved: "
              << argv[2] << '\n';

    return 0;
}
