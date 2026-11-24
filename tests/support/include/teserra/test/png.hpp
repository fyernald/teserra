#pragma once

// Test helper: writes PNG files in memory with stored (uncompressed) deflate blocks, so that tests control
// every byte of the file. #include <teserra/test/png.hpp>
//
//   const std::vector<std::byte> file = test::writePng(2, 1, test::PngColor::Rgba, rgbaSamples);

#include <teserra/core/types.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace teserra::test {

/// PNG color types (PNG specification, IHDR).
enum class PngColor : u8 {
    Gray = 0,
    Rgb = 2,
    Rgba = 6,
};

inline usize channelCount(PngColor color) {
    switch (color) {
        case PngColor::Gray:
            return 1;
        case PngColor::Rgb:
            return 3;
        case PngColor::Rgba:
            return 4;
    }
    return 0;
}

inline u32 crc32(std::span<const u8> data) {
    u32 crc = 0xFFFFFFFFU;
    for (const u8 byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

inline u32 adler32(std::span<const u8> data) {
    u32 a = 1;
    u32 b = 0;
    for (const u8 byte : data) {
        a = (a + byte) % 65521U;
        b = (b + a) % 65521U;
    }
    return (b << 16U) | a;
}

inline void appendU32(std::vector<u8>& out, u32 value) {
    out.push_back(static_cast<u8>(value >> 24U));
    out.push_back(static_cast<u8>(value >> 16U));
    out.push_back(static_cast<u8>(value >> 8U));
    out.push_back(static_cast<u8>(value));
}

inline void appendChunk(std::vector<u8>& out, std::string_view type, std::span<const u8> data) {
    appendU32(out, static_cast<u32>(data.size()));
    const usize start = out.size();
    out.insert(out.end(), type.begin(), type.end());
    out.insert(out.end(), data.begin(), data.end());
    appendU32(out, crc32(std::span(out).subspan(start)));
}

// zlib stream of stored (uncompressed) deflate blocks.
inline std::vector<u8> zlibStored(std::span<const u8> data) {
    std::vector<u8> out{0x78, 0x01};
    usize offset = 0;
    do {
        const usize size = std::min<usize>(data.size() - offset, 0xFFFF);
        const bool last = offset + size == data.size();
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<u8>(size));
        out.push_back(static_cast<u8>(size >> 8U));
        out.push_back(static_cast<u8>(~size));
        out.push_back(static_cast<u8>(~size >> 8U));
        out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(offset),
                   data.begin() + static_cast<std::ptrdiff_t>(offset + size));
        offset += size;
    } while (offset < data.size());
    appendU32(out, adler32(data));
    return out;
}

/// An 8-bit PNG of `samples` (rows top first, `channelCount(color)` samples per pixel). Without samples,
/// the file has no image data: enough for a header that claims any size.
inline std::vector<std::byte> writePng(u32 width, u32 height, PngColor color,
                                       std::span<const u8> samples = {}) {
    std::vector<u8> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

    std::vector<u8> header;
    appendU32(header, width);
    appendU32(header, height);
    header.insert(header.end(),
                  {8, static_cast<u8>(color), 0, 0, 0}); // depth, color, deflate, filter, no interlace
    appendChunk(out, "IHDR", header);

    if (!samples.empty()) {
        const usize rowSize = width * channelCount(color);
        REQUIRE(samples.size() == rowSize * height);
        std::vector<u8> raw;
        for (usize row = 0; row < height; ++row) {
            raw.push_back(0); // filter type None
            const auto rowSamples = samples.subspan(row * rowSize, rowSize);
            raw.insert(raw.end(), rowSamples.begin(), rowSamples.end());
        }
        appendChunk(out, "IDAT", zlibStored(raw));
    }
    appendChunk(out, "IEND", {});

    std::vector<std::byte> file(out.size());
    std::ranges::transform(out, file.begin(), [](u8 byte) { return std::byte{byte}; });
    return file;
}

} // namespace teserra::test
