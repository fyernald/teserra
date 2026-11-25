#include "image_compare.hpp"

#include <teserra/assets/image.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iterator>
#include <string>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

namespace teserra::snapshot {

namespace {

Error ioError(std::string message) {
    return Error{.code = ErrorCode::IoFailure, .module = Module::Assets, .message = std::move(message)};
}

u8 channelDifference(u8 a, u8 b) noexcept {
    return static_cast<u8>(a > b ? a - b : b - a);
}

} // namespace

Comparison compare(const Image& actual, const Image& golden, const Tolerance& tolerance) {
    Comparison result;
    if (actual.width != golden.width || actual.height != golden.height ||
        actual.rgba.size() != golden.rgba.size()) {
        result.sameSize = false;
        return result;
    }
    const usize pixels = usize{actual.width} * actual.height;
    for (usize pixel = 0; pixel < pixels; ++pixel) {
        u8 largest = 0;
        for (usize channel = 0; channel < 4; ++channel) {
            const usize at = (pixel * 4) + channel;
            largest = std::max(largest, channelDifference(actual.rgba[at], golden.rgba[at]));
        }
        result.maxChannelDifference = std::max(result.maxChannelDifference, largest);
        if (largest > tolerance.channel) {
            ++result.differingPixels;
        }
    }
    result.differingRatio =
        pixels == 0 ? 0.0 : static_cast<f64>(result.differingPixels) / static_cast<f64>(pixels);
    result.passed = result.differingRatio <= tolerance.maxDifferingRatio;
    return result;
}

Image diffImage(const Image& actual, const Image& golden, const Tolerance& tolerance) {
    if (actual.width != golden.width || actual.height != golden.height ||
        actual.rgba.size() != golden.rgba.size()) {
        return {};
    }
    Image diff{.width = actual.width, .height = actual.height, .rgba = std::vector<u8>(actual.rgba.size())};
    for (usize pixel = 0; pixel < diff.rgba.size() / 4; ++pixel) {
        const usize at = pixel * 4;
        u8 largest = 0;
        for (usize channel = 0; channel < 4; ++channel) {
            largest =
                std::max(largest, channelDifference(actual.rgba[at + channel], golden.rgba[at + channel]));
        }
        if (largest > tolerance.channel) {
            diff.rgba[at] = 255;
            diff.rgba[at + 1] = 0;
            diff.rgba[at + 2] = 0;
        } else {
            // Rec. 709 luma of the golden, faded toward mid grey.
            const u32 luma =
                ((54U * golden.rgba[at]) + (183U * golden.rgba[at + 1]) + (19U * golden.rgba[at + 2])) / 256U;
            const auto faded = static_cast<u8>(64U + (luma / 2U));
            diff.rgba[at] = faded;
            diff.rgba[at + 1] = faded;
            diff.rgba[at + 2] = faded;
        }
        diff.rgba[at + 3] = 255;
    }
    return diff;
}

Result<Image> readPng(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Unexpected(ioError(std::format("Cannot open '{}'", path.string())));
    }
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    TESERRA_TRY_ASSIGN(assets::ImageData decoded, assets::decodeImage(std::as_bytes(std::span(bytes))));
    Image image{
        .width = decoded.width, .height = decoded.height, .rgba = std::vector<u8>(decoded.pixels.size())};
    std::ranges::transform(decoded.pixels, image.rgba.begin(),
                           [](std::byte value) { return static_cast<u8>(value); });
    return image;
}

Result<void> writePng(const std::filesystem::path& path, const Image& image) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return Unexpected(
            ioError(std::format("Cannot create '{}': {}", path.parent_path().string(), error.message())));
    }
    const int written =
        stbi_write_png(path.string().c_str(), static_cast<int>(image.width), static_cast<int>(image.height),
                       4, image.rgba.data(), static_cast<int>(image.width * 4));
    if (written == 0) {
        return Unexpected(ioError(std::format("Cannot write '{}'", path.string())));
    }
    return {};
}

} // namespace teserra::snapshot
