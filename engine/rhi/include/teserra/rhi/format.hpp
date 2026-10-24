#pragma once

#include <teserra/core/flags.hpp>
#include <teserra/core/types.hpp>

#include <string_view>

namespace teserra::rhi {

/// Pixel format of textures and render targets. Names give the components in memory order, their bits
/// and how they are read: Unorm (0..1), Srgb (0..1, decoded from sRGB when sampled and encoded when
/// written), Float, Uint.
enum class Format : u8 {
    Undefined, ///< No format: an unused color target or no depth attachment.
    R8Unorm,
    Rg8Unorm,
    Rgba8Unorm,
    Rgba8Srgb,
    Bgra8Unorm,
    Bgra8Srgb,
    Rgb10A2Unorm,
    R16Float,
    Rg16Float,
    Rgba16Float,
    R32Float,
    Rg32Float,
    Rgba32Float,
    R32Uint,
    Depth16Unorm,
    /// Not supported by every Vulkan device; Depth32FloatStencil8 is the portable stencil format.
    Depth24UnormStencil8,
    Depth32Float,
    Depth32FloatStencil8,
};

/// Number of Format values.
inline constexpr usize kFormatCount = static_cast<usize>(Format::Depth32FloatStencil8) + 1;

/// What textures of a format can be used for on every device: what OpenGL 4.5 and core Vulkan both
/// guarantee. Validation checks texture usages, render targets and blending against it.
enum class FormatCapabilities : u8 {
    None = 0,
    /// Read in shaders (TextureUsage::Sampled).
    Sampled = 1U << 0U,
    /// Sampled with linear filtering; also needed to generate mipmaps.
    Filterable = 1U << 1U,
    /// Color render target (TextureUsage::RenderTarget); also needed to generate mipmaps.
    RenderTarget = 1U << 2U,
    /// Blended as a render target.
    Blendable = 1U << 3U,
    /// Depth-stencil attachment (TextureUsage::DepthStencil).
    DepthStencil = 1U << 4U,
    /// Written in shaders (TextureUsage::Storage).
    Storage = 1U << 5U,
};
TESERRA_FLAGS(FormatCapabilities)

/// Properties of a Format.
struct FormatInfo {
    std::string_view name;
    /// Bytes per texel; for depth-stencil formats the size of the packed texel as uploads and readbacks
    /// see it. 0 for Undefined.
    u32 bytesPerTexel = 0;
    /// Number of components (color channels, or depth and stencil).
    u32 componentCount = 0;
    bool hasDepth = false;
    bool hasStencil = false;
    /// Color values are sRGB-encoded in memory.
    bool isSrgb = false;
    /// What textures of the format can be used for (the format support table).
    FormatCapabilities capabilities = FormatCapabilities::None;
};

/// Properties of `format`. Asserts that `format` is a valid enumerator.
[[nodiscard]] const FormatInfo& formatInfo(Format format);

/// True for formats with a depth or stencil component, which only depth-stencil attachments may use.
[[nodiscard]] inline bool isDepthStencil(Format format) {
    const FormatInfo& info = formatInfo(format);
    return info.hasDepth || info.hasStencil;
}

/// True if the format support table gives `format` every capability in `capabilities`.
[[nodiscard]] inline bool formatSupports(Format format, FormatCapabilities capabilities) {
    return hasAll(formatInfo(format).capabilities, capabilities);
}

/// Format of one vertex attribute: component type and count. Unorm8x4 is four bytes read as 0..1 floats
/// (typically a color).
enum class VertexFormat : u8 {
    Float,
    Float2,
    Float3,
    Float4,
    Unorm8x4,
    Uint8x4,
    Uint16x2,
    Uint32,
};

/// Number of VertexFormat values.
inline constexpr usize kVertexFormatCount = static_cast<usize>(VertexFormat::Uint32) + 1;

/// Properties of a VertexFormat.
struct VertexFormatInfo {
    std::string_view name;
    u32 sizeInBytes = 0;
    u32 componentCount = 0;
};

/// Properties of `format`. Asserts that `format` is a valid enumerator.
[[nodiscard]] const VertexFormatInfo& vertexFormatInfo(VertexFormat format);

/// Type of the indices in an index buffer.
enum class IndexType : u8 {
    Uint16,
    Uint32,
};

/// Size of one index of `type` in bytes.
[[nodiscard]] constexpr u32 indexSize(IndexType type) noexcept {
    return type == IndexType::Uint16 ? 2U : 4U;
}

} // namespace teserra::rhi
