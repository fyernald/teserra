// The snapshot comparator: per-channel tolerance, the ratio of differing pixels, size mismatches, the diff
// image, and PNG round trips. No GPU needed.

#include "image_compare.hpp"
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using namespace teserra;
using namespace teserra::snapshot;

namespace {

Image solid(u32 width, u32 height, u8 r, u8 g, u8 b) {
    Image image{.width = width, .height = height, .rgba = {}};
    for (u32 i = 0; i < width * height; ++i) {
        image.rgba.insert(image.rgba.end(), {r, g, b, 255});
    }
    return image;
}

} // namespace

TEST_CASE("Snapshot comparator: equal images and small channel differences pass", "[snapshot][comparator]") {
    const Image golden = solid(10, 10, 100, 150, 200);
    CHECK(compare(golden, golden, {}).passed);
    CHECK(compare(golden, golden, {}).maxChannelDifference == 0);

    Image actual = golden;
    actual.rgba[0] = 102; // within the default 2/255
    const Comparison within = compare(actual, golden, {});
    CHECK(within.passed);
    CHECK(within.differingPixels == 0);
    CHECK(within.maxChannelDifference == 2);
}

TEST_CASE("Snapshot comparator: the ratio of differing pixels decides", "[snapshot][comparator]") {
    const Image golden = solid(10, 10, 0, 0, 0);
    Image actual = golden;
    actual.rgba[4 * 7 + 1] = 40; // one pixel of 100: 1%

    const Comparison strict = compare(actual, golden, {.channel = 2, .maxDifferingRatio = 0.001});
    CHECK_FALSE(strict.passed);
    CHECK(strict.differingPixels == 1);
    CHECK(strict.differingRatio == 0.01);
    CHECK(strict.maxChannelDifference == 40);

    CHECK(compare(actual, golden, {.channel = 2, .maxDifferingRatio = 0.01}).passed);
    CHECK(compare(actual, golden, {.channel = 40, .maxDifferingRatio = 0.0}).passed);
}

TEST_CASE("Snapshot comparator: images of different sizes never pass", "[snapshot][comparator]") {
    const Comparison result =
        compare(solid(4, 4, 0, 0, 0), solid(4, 5, 0, 0, 0), {.channel = 255, .maxDifferingRatio = 1.0});
    CHECK_FALSE(result.sameSize);
    CHECK_FALSE(result.passed);
    CHECK(diffImage(solid(4, 4, 0, 0, 0), solid(4, 5, 0, 0, 0), {}).empty());
}

TEST_CASE("Snapshot comparator: the diff image marks differing pixels in red", "[snapshot][comparator]") {
    const Image golden = solid(2, 1, 200, 200, 200);
    Image actual = golden;
    actual.rgba[4] = 0; // the second pixel
    const Image diff = diffImage(actual, golden, {});
    REQUIRE(diff.rgba.size() == 8);
    CHECK(diff.rgba[0] == diff.rgba[1]); // grey
    CHECK(diff.rgba[4] == 255);          // red
    CHECK(diff.rgba[5] == 0);
    CHECK(diff.rgba[6] == 0);
    CHECK(diff.rgba[7] == 255);
}

TEST_CASE("Snapshot comparator: PNG files round-trip", "[snapshot][comparator]") {
    Image image = solid(3, 2, 10, 20, 30);
    image.rgba[3] = 128; // a translucent pixel survives too
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "teserra_snapshot_test" / "round_trip.png";
    REQUIRE(writePng(path, image).has_value());
    const Result<Image> read = readPng(path);
    REQUIRE(read.has_value());
    CHECK(read->width == 3);
    CHECK(read->height == 2);
    CHECK(read->rgba == image.rgba);
    std::filesystem::remove_all(path.parent_path());
    CHECK_FALSE(readPng(path).has_value());
}
