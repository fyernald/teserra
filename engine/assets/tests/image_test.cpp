// Image decoding: pixels and orientation, conversions to RGBA8, malformed data and the size limits.
//
// The PNG files are written by the test itself (stored deflate blocks, no compression), so that headers,
// sizes and corruptions can be chosen exactly.

#include <teserra/assets/image.hpp>

#include <teserra/test/allocation_counter.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace teserra;
using namespace teserra::assets;
using Catch::Matchers::ContainsSubstring;

namespace {

/// PNG color types used here (PNG specification, IHDR).
enum class PngColor : u8 {
    Gray = 0,
    Rgb = 2,
    Rgba = 6,
};

usize channelCount(PngColor color) {
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

u32 crc32(std::span<const u8> data) {
    u32 crc = 0xFFFFFFFFU;
    for (const u8 byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

u32 adler32(std::span<const u8> data) {
    u32 a = 1;
    u32 b = 0;
    for (const u8 byte : data) {
        a = (a + byte) % 65521U;
        b = (b + a) % 65521U;
    }
    return (b << 16U) | a;
}

void appendU32(std::vector<u8>& out, u32 value) {
    out.push_back(static_cast<u8>(value >> 24U));
    out.push_back(static_cast<u8>(value >> 16U));
    out.push_back(static_cast<u8>(value >> 8U));
    out.push_back(static_cast<u8>(value));
}

void appendChunk(std::vector<u8>& out, std::string_view type, std::span<const u8> data) {
    appendU32(out, static_cast<u32>(data.size()));
    const usize start = out.size();
    out.insert(out.end(), type.begin(), type.end());
    out.insert(out.end(), data.begin(), data.end());
    appendU32(out, crc32(std::span(out).subspan(start)));
}

// zlib stream of stored (uncompressed) deflate blocks.
std::vector<u8> zlibStored(std::span<const u8> data) {
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
std::vector<std::byte> writePng(u32 width, u32 height, PngColor color, std::span<const u8> samples = {}) {
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

std::vector<std::byte> toBytes(std::span<const u8> values) {
    std::vector<std::byte> bytes(values.size());
    std::ranges::transform(values, bytes.begin(), [](u8 value) { return std::byte{value}; });
    return bytes;
}

// 2 x 3 RGBA image, every pixel different: red and green on the top row, blue and white in the middle,
// half-transparent gray and transparent black at the bottom.
constexpr std::array<u8, 24> kRgbaPixels{
    255, 0,   0,   255, 0,   255, 0,   255, // row 0 (top)
    0,   0,   255, 255, 255, 255, 255, 255, // row 1
    128, 128, 128, 128, 0,   0,   0,   0,   // row 2 (bottom)
};

std::vector<std::byte> rgbaPng() {
    return writePng(2, 3, PngColor::Rgba, kRgbaPixels);
}

// A larger image, so that decoding it makes the decoder allocate.
std::vector<std::byte> gradientPng() {
    constexpr u32 kSize = 64;
    std::vector<u8> samples;
    for (u32 y = 0; y < kSize; ++y) {
        for (u32 x = 0; x < kSize; ++x) {
            samples.insert(samples.end(), {static_cast<u8>(x * 4), static_cast<u8>(y * 4), 0, 255});
        }
    }
    return writePng(kSize, kSize, PngColor::Rgba, samples);
}

void requireError(const Result<ImageData>& result, ErrorCode code) {
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == code);
    CHECK(result.error().module == Module::Assets);
}

} // namespace

TEST_CASE("decodeImage decodes an RGBA PNG with row 0 as the top row of the file", "[assets][image]") {
    const auto result = decodeImage(rgbaPng());

    REQUIRE(result.has_value());
    CHECK(result->width == 2);
    CHECK(result->height == 3);
    CHECK(result->format == ImageFormat::Rgba8);
    CHECK(result->rowPitch() == 8);
    // Conventions (ADR-0024): no vertical flip, so the bytes are the file's rows in order.
    CHECK(result->pixels == toBytes(kRgbaPixels));
}

TEST_CASE("decodeImage converts RGB and grayscale PNGs to RGBA8 with opaque alpha", "[assets][image]") {
    SECTION("RGB") {
        constexpr std::array<u8, 6> kRgb{10, 20, 30, 40, 50, 60};
        const auto result = decodeImage(writePng(1, 2, PngColor::Rgb, kRgb));

        REQUIRE(result.has_value());
        constexpr std::array<u8, 8> kExpected{10, 20, 30, 255, 40, 50, 60, 255};
        CHECK(result->pixels == toBytes(kExpected));
    }
    SECTION("grayscale") {
        constexpr std::array<u8, 2> kGray{0, 200};
        const auto result = decodeImage(writePng(2, 1, PngColor::Gray, kGray));

        REQUIRE(result.has_value());
        constexpr std::array<u8, 8> kExpected{0, 0, 0, 255, 200, 200, 200, 255};
        CHECK(result->pixels == toBytes(kExpected));
    }
}

TEST_CASE("decodeImage returns InvalidData for data that is not a PNG image", "[assets][image]") {
    SECTION("empty") {
        requireError(decodeImage({}), ErrorCode::InvalidData);
    }
    SECTION("text") {
        constexpr std::string_view kText = "This is not an image.";
        requireError(decodeImage(std::as_bytes(std::span(kText))), ErrorCode::InvalidData);
    }
    SECTION("JPEG, which this build does not decode") {
        constexpr std::array<u8, 11> kJpegStart{0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00};
        const auto result = decodeImage(toBytes(kJpegStart));

        requireError(result, ErrorCode::InvalidData);
        CHECK_THAT(result.error().message, ContainsSubstring("Cannot read the image header"));
    }
}

TEST_CASE("decodeImage returns InvalidData for every truncation of a PNG file", "[assets][image]") {
    const std::vector<std::byte> file = rgbaPng();

    // Up to the CRC of the closing IEND chunk: stb_image stops at the chunk type, so a file without those
    // last 4 bytes still holds the whole image and decodes.
    for (usize size = 1; size < file.size() - 4; ++size) {
        INFO("first " << size << " of " << file.size() << " bytes");
        requireError(decodeImage(std::span(file).first(size)), ErrorCode::InvalidData);
    }
}

TEST_CASE("decodeImage survives a PNG file with any one byte corrupted", "[assets][image]") {
    const std::vector<std::byte> original = rgbaPng();

    // Not every corruption is detectable (stb_image does not check CRCs), but each one either fails with
    // an error or decodes to a consistent image.
    for (usize index = 0; index < original.size(); ++index) {
        INFO("byte " << index << " inverted");
        std::vector<std::byte> file = original;
        file[index] = ~file[index];

        const auto result = decodeImage(file);
        if (result.has_value()) {
            CHECK(result->pixels.size() == result->rowPitch() * result->height);
        } else {
            CHECK((result.error().code == ErrorCode::InvalidData ||
                   result.error().code == ErrorCode::Unsupported));
        }
    }
}

TEST_CASE("decodeImage rejects an image wider or taller than maxDimension", "[assets][image]") {
    SECTION("at the limit") {
        CHECK(decodeImage(rgbaPng(), {.maxDimension = 3}).has_value());
    }
    SECTION("over the limit") {
        const auto result = decodeImage(rgbaPng(), {.maxDimension = 2});

        requireError(result, ErrorCode::Unsupported);
        CHECK_THAT(result.error().message, ContainsSubstring("2x3 pixels"));
    }
    SECTION("over the default limit, from the header alone") {
        // No image data at all: the header is rejected before the decoder would look for it.
        const auto result = decodeImage(writePng(16385, 1, PngColor::Rgba));

        requireError(result, ErrorCode::Unsupported);
        CHECK_THAT(result.error().message, ContainsSubstring("16385x1 pixels"));
    }
}

TEST_CASE("decodeImage rejects an image over maxSizeInBytes before it allocates", "[assets][image]") {
    SECTION("at the limit") {
        CHECK(decodeImage(rgbaPng(), {.maxSizeInBytes = 24}).has_value());
    }
    SECTION("over the limit") {
        const auto result = decodeImage(rgbaPng(), {.maxSizeInBytes = 23});

        requireError(result, ErrorCode::Unsupported);
        CHECK_THAT(result.error().message, ContainsSubstring("24 bytes"));
    }
    SECTION("nothing is allocated for the pixels") {
        const std::vector<std::byte> file = gradientPng();

        u64 decodeAllocations = 0;
        {
            const test::AllocationCounter allocations;
            const auto result = decodeImage(file);
            decodeAllocations = allocations.count();
            REQUIRE(result.has_value());
        }
        u64 rejectAllocations = 0;
        std::string rejectMessage;
        {
            const test::AllocationCounter allocations;
            const auto result = decodeImage(file, {.maxSizeInBytes = 1024});
            rejectAllocations = allocations.count();
            requireError(result, ErrorCode::Unsupported);
            rejectMessage = result.error().message;
        }
        // The same error made directly: what a rejection may allocate (with MSVC's debug iterators, a
        // string allocates more than its characters).
        u64 errorAllocations = 0;
        {
            const test::AllocationCounter allocations;
            const Result<ImageData> error =
                fail(ErrorCode::Unsupported, Module::Assets,
                     "Image of {}x{} pixels decodes to {} bytes, over the limit of {} bytes", u32{64},
                     u32{64}, u64{16384}, usize{1024});
            errorAllocations = allocations.count();
            REQUIRE(error.error().message == rejectMessage);
        }

        // Decoding allocates the decoder's state and buffers (through operator new) and the pixels; a
        // rejected image allocates only its error message.
        CHECK(decodeAllocations > errorAllocations);
        CHECK(rejectAllocations == errorAllocations);
    }
}
