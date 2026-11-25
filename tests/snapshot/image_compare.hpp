#pragma once

// Snapshot images (docs/guides/testing-strategy.md#snapshot-tests): RGBA8 images, their comparison with a
// per-channel tolerance and a maximum ratio of differing pixels, the diff image of a failed comparison, and
// PNG files.

#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>

#include <filesystem>
#include <vector>

namespace teserra::snapshot {

/// An RGBA8 image, rows top first.
struct Image {
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> rgba;

    [[nodiscard]] bool empty() const noexcept { return width == 0 || height == 0; }
};

/// How far an image may be from its golden. Software and hardware rasterizers differ slightly at edges and
/// in blending, so each test can widen these.
struct Tolerance {
    /// Largest difference of one channel that still counts as equal, in 1/255 steps.
    u8 channel = 2;
    /// Largest share of pixels that may differ, 0 to 1 (0.001 = 0.1%).
    f64 maxDifferingRatio = 0.001;
};

struct Comparison {
    bool sameSize = true;
    /// Pixels with a channel that differs by more than the tolerance.
    usize differingPixels = 0;
    /// Share of all pixels that differ.
    f64 differingRatio = 0.0;
    /// The largest channel difference over all pixels.
    u8 maxChannelDifference = 0;
    /// The images match within the tolerance.
    bool passed = false;
};

/// Compares `actual` with `golden` within `tolerance`. Images of different sizes never pass.
[[nodiscard]] Comparison compare(const Image& actual, const Image& golden, const Tolerance& tolerance);

/// A picture of the difference: pixels that differ beyond `tolerance` in opaque red, the others as a
/// faded grey copy of `golden`, so the failure is easy to place. Empty for images of different sizes.
[[nodiscard]] Image diffImage(const Image& actual, const Image& golden, const Tolerance& tolerance);

/// Reads a PNG file as RGBA8.
[[nodiscard]] Result<Image> readPng(const std::filesystem::path& path);
/// Writes `image` as a compressed PNG file, creating its directory.
[[nodiscard]] Result<void> writePng(const std::filesystem::path& path, const Image& image);

} // namespace teserra::snapshot
