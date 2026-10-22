#pragma once

#include <teserra/core/flags.hpp>
#include <teserra/core/types.hpp>
#include <teserra/rhi/format.hpp>

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

} // namespace teserra::rhi
