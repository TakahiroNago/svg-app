#include "converter.hpp"
#include "engine_common.hpp"

#include "svgconv/v1/svgconv.grpc.pb.h"

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <opencv2/core.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace pb = svgconv::v1;
using grpc::Status;
using grpc::StatusCode;

// Mirrors kAllowedColors in converter.cpp so bad requests map to INVALID_ARGUMENT.
constexpr std::array<int, 10> kAllowedColors{2, 3, 4, 5, 6, 8, 12, 16, 24, 32};

// The receive limit must cover the largest image the engine accepts (64 MiB of RGBA).
constexpr int kMaxMessageBytes = 80 * 1024 * 1024;

class ConcurrencyGuard {
public:
    ConcurrencyGuard(std::atomic<int>& counter, int limit) : counter_(counter) {
        acquired_ = counter_.fetch_add(1) < limit;
        if (!acquired_) {
            counter_.fetch_sub(1);
        }
    }
    ~ConcurrencyGuard() {
        if (acquired_) {
            counter_.fetch_sub(1);
        }
    }
    ConcurrencyGuard(const ConcurrencyGuard&) = delete;
    ConcurrencyGuard& operator=(const ConcurrencyGuard&) = delete;
    bool acquired() const { return acquired_; }

private:
    std::atomic<int>& counter_;
    bool acquired_ = false;
};

class SvgConverterServiceImpl final : public pb::SvgConverterService::Service {
public:
    explicit SvgConverterServiceImpl(int maxConcurrency) : maxConcurrency_(maxConcurrency) {}

    Status Convert(grpc::ServerContext* context, const pb::ConvertRequest* request,
                   pb::ConvertResponse* response) override {
        try {
            return convert(context, *request, *response);
        } catch (const std::exception& e) {
            return {StatusCode::INTERNAL, e.what()};
        } catch (...) {
            return {StatusCode::INTERNAL, "Unexpected error during conversion."};
        }
    }

private:
    Status convert(grpc::ServerContext* context, const pb::ConvertRequest& request,
                   pb::ConvertResponse& response) {
        if (!request.has_image()) {
            return {StatusCode::INVALID_ARGUMENT, "image is required."};
        }
        const pb::Image& image = request.image();
        const int width = image.width();
        const int height = image.height();

        std::string error;
        if (width <= 0 || height <= 0) {
            return {StatusCode::INVALID_ARGUMENT, "Image width and height must be positive."};
        }
        if (!svg_app::validateDimensions(width, height, error)) {
            return {StatusCode::RESOURCE_EXHAUSTED, error};
        }
        if (!svg_app::validateRgbaLength(image.rgba().size(), width, height, error)) {
            return {StatusCode::INVALID_ARGUMENT, error};
        }
        const int colorCount = request.color_count();
        if (std::find(kAllowedColors.begin(), kAllowedColors.end(), colorCount) ==
            kAllowedColors.end()) {
            return {StatusCode::INVALID_ARGUMENT, "Unsupported color count."};
        }
        if (static_cast<std::int64_t>(width) * height < colorCount) {
            return {StatusCode::INVALID_ARGUMENT,
                    std::format("Image is too small for {} colors.", colorCount)};
        }
        if (context->IsCancelled()) {
            return {StatusCode::CANCELLED, "Request cancelled."};
        }

        const ConcurrencyGuard guard(inFlight_, maxConcurrency_);
        if (!guard.acquired()) {
            return {StatusCode::RESOURCE_EXHAUSTED, "Engine is busy. Try again later."};
        }

        const cv::Mat bgr = svg_app::rgbaToBgrOnWhite(
            reinterpret_cast<const std::uint8_t*>(image.rgba().data()), width, height);

        svg_app::ImageAnalysis analysis;
        if (!svg_app::analyzeImage(bgr, colorCount, analysis, error)) {
            return {StatusCode::INTERNAL, error};
        }

        svg_app::SvgOptions options;
        options.boundary = request.boundary();
        options.keptColors.assign(request.kept_colors().begin(), request.kept_colors().end());
        svg_app::SvgResult result;
        if (!svg_app::generateSvg(analysis, options, result, error)) {
            return {StatusCode::INVALID_ARGUMENT, error};
        }

        response.set_svg(std::move(result.svg));
        response.set_contour_count(result.contourCount);
        for (const int index : analysis.order) {
            const auto i = static_cast<std::size_t>(index);
            pb::PaletteColor* color = response.add_palette();
            color->set_index(index);
            color->set_hex(svg_app::colorToHex(analysis.palette[i]));
            color->set_area(analysis.area[i]);
        }
        return Status::OK;
    }

    const int maxConcurrency_;
    std::atomic<int> inFlight_{0};
};

std::string envOr(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return (value != nullptr && *value != '\0') ? value : fallback;
}

std::atomic<bool> g_stop{false};

void onSignal(int) { g_stop = true; }

}  // namespace

int main() {
    const std::string address = envOr("SVG_GRPC_ADDR", "0.0.0.0:50051");
    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    int maxConcurrency = static_cast<int>(hardware);
    try {
        maxConcurrency = std::stoi(envOr("SVG_MAX_CONCURRENCY", std::to_string(hardware)));
    } catch (...) {
        std::cerr << "Invalid SVG_MAX_CONCURRENCY; using " << maxConcurrency << '\n';
    }
    maxConcurrency = std::max(1, maxConcurrency);

    SvgConverterServiceImpl service(maxConcurrency);
    grpc::EnableDefaultHealthCheckService(true);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.SetMaxReceiveMessageSize(kMaxMessageBytes);
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
    if (!server) {
        std::cerr << "Failed to start gRPC server on " << address << '\n';
        return 1;
    }
    server->GetHealthCheckService()->SetServingStatus(true);

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::thread watcher([&] {
        while (!g_stop) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(10));
    });

    std::cout << "svg-grpc-server listening on " << address
              << " (max concurrency " << maxConcurrency << ")" << std::endl;
    server->Wait();
    g_stop = true;
    watcher.join();
    return 0;
}
