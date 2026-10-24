#pragma once

#include <teserra/core/flags.hpp>
#include <teserra/core/types.hpp>
#include <teserra/rhi/format.hpp>

#include <algorithm>
#include <bit>
#include <limits>
#include <string_view>

namespace teserra::rhi {

/// Shape of a texture. v1 uses Tex2D and Tex2DArray; Tex3D and Cube are defined so that 3D rendering can
/// use them without changing this type.
enum class TextureDimension : u8 {
    Tex2D,
    Tex2DArray,
    Tex3D,
    Cube,
};

/// How a texture may be used. A texture is created with every usage it will ever have.
enum class TextureUsage : u8 {
    None = 0,
    /// Read in shaders through a bind group.
    Sampled = 1U << 0U,
    /// Color attachment of render passes.
    RenderTarget = 1U << 1U,
    /// Depth-stencil attachment of render passes.
    DepthStencil = 1U << 2U,
    /// Written in shaders (future: compute).
    Storage = 1U << 3U,
    /// Source of copy commands, e.g. readback.
    CopySrc = 1U << 4U,
    /// Destination of copy commands and Device uploads.
    CopyDst = 1U << 5U,
};
TESERRA_FLAGS(TextureUsage)

/// TextureDesc::mipLevels value for the full mip chain down to 1x1.
inline constexpr u32 kAllMips = std::numeric_limits<u32>::max();

/// Creation parameters of a texture. Texel row 0 is the top row (docs/architecture/rhi.md#conventions).
struct TextureDesc {
    TextureDimension dimension = TextureDimension::Tex2D;
    Format format = Format::Rgba8Srgb;
    u32 width = 1;
    u32 height = 1;
    /// Depth of a Tex3D, layers of a Tex2DArray; 6 for a Cube; 1 otherwise.
    u32 depthOrLayers = 1;
    /// Number of mip levels, or kAllMips.
    u32 mipLevels = 1;
    u32 sampleCount = 1;
    TextureUsage usage = TextureUsage::Sampled | TextureUsage::CopyDst;
    /// Shown by graphics debuggers and in validation messages.
    std::string_view debugName{};
};

/// Mip levels of a full chain down to 1x1 for a texture whose larger side is `largestDimension`.
[[nodiscard]] constexpr u32 fullMipChain(u32 largestDimension) noexcept {
    return static_cast<u32>(std::bit_width(largestDimension));
}

/// The number of mip levels of `desc`, with kAllMips resolved to the full chain.
[[nodiscard]] constexpr u32 mipLevelCount(const TextureDesc& desc) noexcept {
    return desc.mipLevels == kAllMips ? fullMipChain(std::max(desc.width, desc.height)) : desc.mipLevels;
}

/// A box of texels in one mip level of a texture: columns x to x + width - 1 and rows y to y + height - 1,
/// where row 0 is the top row (docs/architecture/rhi.md#conventions), of the layers arrayLayer to
/// arrayLayer + layerCount - 1.
struct TextureRegion {
    u32 mipLevel = 0;
    /// First array layer.
    u32 arrayLayer = 0;
    u32 layerCount = 1;
    /// Top-left texel of the region.
    u32 x = 0;
    u32 y = 0;
    u32 width = 0;
    u32 height = 0;
};

/// Bytes that texel data for `region` of a texture of `format` spans: `region.height` rows per layer, each
/// `bytesPerRow` apart (0 means tightly packed), layers one after another, and the last row without
/// padding.
[[nodiscard]] inline u64 textureDataSize(Format format, const TextureRegion& region, u32 bytesPerRow) {
    const u64 rowBytes = u64{region.width} * formatInfo(format).bytesPerTexel;
    const u64 pitch = bytesPerRow != 0 ? bytesPerRow : rowBytes;
    const u64 rows = u64{region.height} * region.layerCount;
    return rows == 0 ? 0 : (pitch * (rows - 1)) + rowBytes;
}

} // namespace teserra::rhi
