
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <iostream>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: svg-app <input.png>\n";
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

    cv::findContours(
        binary,
        contours,
        cv::RETR_LIST,
        cv::CHAIN_APPROX_SIMPLE
    );

    std::cout << "OpenCV version: "
              << CV_VERSION << '\n';

    std::cout << "Image size: "
              << image.cols << " x "
              << image.rows << '\n';

    std::cout << "Contours found: "
              << contours.size() << '\n';

    return 0;
}
