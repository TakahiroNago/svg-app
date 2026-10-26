
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

enum class BoundaryMode {
    None,
    All,
    Black,
    White
};

int main(int argc, char* argv[]) {
    BoundaryMode boundaryMode = BoundaryMode::None;
    if (argc == 4) {
        const std::string_view option(argv[3]);
        if (option == "--boundary") {
            boundaryMode = BoundaryMode::All;
        } else if (option == "--boundary-black") {
            boundaryMode = BoundaryMode::Black;
        } else if (option == "--boundary-white") {
            boundaryMode = BoundaryMode::White;
        }
    }
    if (argc < 3 || argc > 4 ||
        (argc == 4 && boundaryMode == BoundaryMode::None)) {
        std::cerr << "Usage: svg-app <input-image> <output.svg> "
                     "[--boundary | --boundary-black | --boundary-white]\n";
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

    if (boundaryMode == BoundaryMode::Black) {
        cv::threshold(image, binary, 64, 255, cv::THRESH_BINARY_INV);
    } else if (boundaryMode == BoundaryMode::White) {
        cv::threshold(image, binary, 192, 255, cv::THRESH_BINARY);
    } else {
        cv::threshold(
            image,
            binary,
            0,
            255,
            cv::THRESH_BINARY_INV | cv::THRESH_OTSU
        );
    }
    std::vector<std::vector<cv::Point>> contours;

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
        << "  <path ";
    if (boundaryMode != BoundaryMode::None) {
        svg << "fill=\"none\" stroke=\"black\" stroke-width=\"1\" d=\"";
    } else {
        svg << "fill=\"black\" fill-rule=\"evenodd\" d=\"";
    }

    for (const auto& contour : contours) {
        if (contour.empty()) {
            continue;
        }

        if ((boundaryMode == BoundaryMode::All) &&
            image.cols > 1 &&
            image.rows > 1 &&
            cv::boundingRect(contour) == cv::Rect(0, 0, image.cols, image.rows)) {
            bool topLeft = false;
            bool topRight = false;
            bool bottomLeft = false;
            bool bottomRight = false;
            bool onImageEdge = true;

            for (const auto& point : contour) {
                topLeft |= point == cv::Point(0, 0);
                topRight |= point == cv::Point(image.cols - 1, 0);
                bottomLeft |= point == cv::Point(0, image.rows - 1);
                bottomRight |= point == cv::Point(image.cols - 1, image.rows - 1);
                onImageEdge &= point.x == 0 ||
                    point.x == image.cols - 1 ||
                    point.y == 0 ||
                    point.y == image.rows - 1;
            }

            if (onImageEdge && topLeft && topRight && bottomLeft && bottomRight) {
                continue;
            }
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
