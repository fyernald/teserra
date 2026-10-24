#pragma once

// CPU images decoded from PNG files (ADR-0010, docs/architecture/assets.md#image-decoding).

#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace teserra::assets {

/// Pixel layout of an ImageData.
enum class ImageFormat : u8 {
    /// Red, green, blue and alpha, 8 bits each, in that byte order. Whether the values are sRGB or linear
    /// is not part of the image: the loader that uploads it decides (ADR-0010).
    Rgba8,
};

/// Bytes per pixel of `format`.
[[nodiscard]] constexpr usize bytesPerPixel(ImageFormat format) noexcept {
    switch (format) {
        case ImageFormat::Rgba8:
            return 4;
    }
    return 0;
}

/// A decoded image in CPU memory. Row 0 is the top row of the image and pixels run left to right in each
/// row (ADR-0024), with no padding between rows.
struct ImageData {
    u32 width = 0;
    u32 height = 0;
    ImageFormat format = ImageFormat::Rgba8;
    /// `height` rows of rowPitch() bytes, top row first.
    std::vector<std::byte> pixels{};

    /// Bytes per row: `width * bytesPerPixel(format)`.
    [[nodiscard]] usize rowPitch() const noexcept { return usize{width} * bytesPerPixel(format); }
};

/// Limits that decodeImage() checks against the image's header, before it allocates the pixels.
struct ImageDecodeConfig {
    /// Largest width and largest height in pixels. The default is the smallest MAX_TEXTURE_SIZE that
    /// OpenGL 4.5 allows, so every accepted image fits in a texture.
    u32 maxDimension = 16384;
    /// Largest decoded size in bytes. The default holds an 8192 x 8192 RGBA8 image.
    usize maxSizeInBytes = usize{256} * 1024 * 1024;
};

/// Decodes a PNG image from memory into RGBA8 pixels, with row 0 as the top row of the file. Grayscale,
/// RGB, palette and 16-bit images are converted; images without alpha get an alpha of 255.
///
/// Errors:
/// - InvalidData: `bytes` is empty, not a PNG image, corrupted or truncated. The message gives the
///   decoder's reason.
/// - Unsupported: the image is larger than `config` allows, or `bytes` is 2 GiB or more.
/// - OutOfMemory: the decoder could not allocate its memory.
///
/// Asset files are trusted developer content in v1: the decoder is not hardened against hostile input
/// (SECURITY.md).
[[nodiscard]] Result<ImageData> decodeImage(std::span<const std::byte> bytes,
                                            const ImageDecodeConfig& config = {});

} // namespace teserra::assets
