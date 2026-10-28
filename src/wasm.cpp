#include "converter.hpp"
#include "engine_common.hpp"

#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <opencv2/core.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace {

using emscripten::val;

val makeResult() {
    val result = val::object();
    result.set("success", false);
    result.set("svg", std::string());
    result.set("contourCount", 0);
    result.set("error", std::string());
    return result;
}

val fail(const std::string& message) {
    val result = makeResult();
    result.set("error", message);
    return result;
}

// Validates the JS input and copies it into WASM memory in a single bulk copy.
bool readPixels(const val& pixelData, int width, int height, cv::Mat& bgr,
                std::string& error) {
    if (!svg_app::validateDimensions(width, height, error)) {
        return false;
    }
    if (pixelData.isNull() || pixelData.isUndefined() ||
        !val::global("ArrayBuffer").call<bool>("isView", pixelData)) {
        error = "Pixel data must be a Uint8Array or Uint8ClampedArray.";
        return false;
    }
    const auto byteLength = pixelData["byteLength"].as<std::uint64_t>();
    if (byteLength == 0) {
        error = "Pixel data is empty.";
        return false;
    }
    if (!svg_app::validateRgbaLength(byteLength, width, height, error)) {
        return false;
    }

    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(byteLength));
    val view(emscripten::typed_memory_view(rgba.size(), rgba.data()));
    view.call<void>("set", val::global("Uint8Array").new_(pixelData["buffer"],
                                                           pixelData["byteOffset"],
                                                           pixelData["byteLength"]));
    bgr = svg_app::rgbaToBgrOnWhite(rgba.data(), width, height);
    return true;
}

std::vector<int> toIntVector(const val& array, std::string& error) {
    std::vector<int> values;
    if (array.isNull() || array.isUndefined()) {
        return values;
    }
    if (!val::global("Array").call<bool>("isArray", array)) {
        error = "keptColors must be an array of palette indices.";
        return values;
    }
    const auto length = array["length"].as<unsigned>();
    values.reserve(length);
    for (unsigned i = 0; i < length; ++i) {
        const val item = array[i];
        if (!item.isNumber() || !val::global("Number").call<bool>("isInteger", item)) {
            error = "keptColors must contain integers only.";
            return {};
        }
        values.push_back(item.as<int>());
    }
    return values;
}

val generate(const svg_app::ImageAnalysis& analysis, const val& keptColors, bool boundary) {
    std::string error;
    svg_app::SvgOptions options;
    options.boundary = boundary;
    options.keptColors = toIntVector(keptColors, error);
    if (!error.empty()) {
        return fail(error);
    }
    svg_app::SvgResult svg;
    if (!svg_app::generateSvg(analysis, options, svg, error)) {
        return fail(error);
    }
    val result = makeResult();
    result.set("success", true);
    result.set("svg", svg.svg);
    result.set("contourCount", static_cast<double>(svg.contourCount));
    return result;
}

val describePalette(const svg_app::ImageAnalysis& analysis) {
    val colors = val::array();
    for (const int index : analysis.order) {
        val item = val::object();
        item.set("index", index);
        item.set("hex", svg_app::colorToHex(analysis.palette[static_cast<std::size_t>(index)]));
        item.set("area", static_cast<double>(analysis.area[static_cast<std::size_t>(index)]));
        colors.call<void>("push", item);
    }
    return colors;
}

// Holds a k-means analysis so colors/boundary can be changed without re-quantizing.
// JS must call delete() when finished.
class SvgSession {
public:
    val analyze(const val& pixelData, int width, int height, int colorCount) {
        try {
            cv::Mat bgr;
            std::string error;
            if (!readPixels(pixelData, width, height, bgr, error)) {
                return fail(error);
            }
            svg_app::ImageAnalysis next;
            if (!svg_app::analyzeImage(bgr, colorCount, next, error)) {
                return fail(error);
            }
            analysis_ = std::move(next);
            ready_ = true;
            val result = makeResult();
            result.set("success", true);
            result.set("palette", describePalette(analysis_));
            result.set("error", std::string());
            return result;
        } catch (const std::exception& e) {
            return fail(e.what());
        } catch (...) {
            return fail("Unexpected error during analysis.");
        }
    }

    val generate(const val& keptColors, bool boundary) {
        try {
            if (!ready_) {
                return fail("analyze() must succeed before generate().");
            }
            return ::generate(analysis_, keptColors, boundary);
        } catch (const std::exception& e) {
            return fail(e.what());
        } catch (...) {
            return fail("Unexpected error during SVG generation.");
        }
    }

private:
    svg_app::ImageAnalysis analysis_;
    bool ready_ = false;
};

val convertImage(const val& pixelData, int width, int height, int colorCount,
                 const val& keptColors, bool boundary) {
    try {
        cv::Mat bgr;
        std::string error;
        if (!readPixels(pixelData, width, height, bgr, error)) {
            return fail(error);
        }
        svg_app::ImageAnalysis analysis;
        if (!svg_app::analyzeImage(bgr, colorCount, analysis, error)) {
            return fail(error);
        }
        return generate(analysis, keptColors, boundary);
    } catch (const std::exception& e) {
        return fail(e.what());
    } catch (...) {
        return fail("Unexpected error during conversion.");
    }
}

}  // namespace

EMSCRIPTEN_BINDINGS(svg_converter_module) {
    emscripten::function("convertImage", &convertImage);
    emscripten::class_<SvgSession>("SvgSession")
        .constructor<>()
        .function("analyze", &SvgSession::analyze)
        .function("generate", &SvgSession::generate);
}
