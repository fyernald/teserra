#pragma once

// Two kinds of limits. The k* constants are bounds of the RHI API itself, the same on every backend: they
// size fixed arrays in descriptors and command records, and recording checks them. DeviceLimits holds what
// the device reports; code that depends on hardware capabilities reads it from Device::limits() instead of
// assuming a backend's value (docs/architecture/rhi.md#what-must-not-leak-out-of-the-rhi).

#include <teserra/core/types.hpp>

namespace teserra::rhi {

/// Color attachments of a render pass and color targets of a pipeline.
inline constexpr u32 kMaxColorAttachments = 4;
/// Vertex buffer bindings of a pipeline and of a draw.
inline constexpr u32 kMaxVertexBuffers = 8;
/// Vertex attributes of a pipeline.
inline constexpr u32 kMaxVertexAttributes = 16;
/// Bind groups of a pipeline layout (set indices 0 to 3).
inline constexpr u32 kMaxBindGroups = 4;
/// Bindings of a bind group (binding numbers 0 to 7).
inline constexpr u32 kMaxBindingsPerGroup = 8;
/// Dynamic offsets passed when binding one bind group.
inline constexpr u32 kMaxDynamicOffsets = 8;
/// Bytes of a debug group or marker label.
inline constexpr u32 kMaxDebugLabelLength = 255;
/// Frames the CPU may record ahead of the GPU; per-frame resources come in this many copies.
inline constexpr u32 kMaxFramesInFlight = 2;

/// Capabilities of a device. The defaults are the minimums every backend guarantees; a device reports
/// its actual values, which are never lower.
struct DeviceLimits {
    /// Largest width or height of a 2D texture.
    u32 maxTextureDimension2D = 8192;
    /// Most layers of a 2D array texture.
    u32 maxTextureArrayLayers = 256;
    /// Required alignment of uniform buffer offsets in bind groups and of dynamic offsets.
    u32 uniformBufferOffsetAlignment = 256;
    /// Largest uniform buffer range one binding can see, in bytes.
    u32 maxUniformBufferRange = 16384;
    /// Highest SamplerDesc::maxAnisotropy that has an effect; 1 means no anisotropic filtering.
    f32 maxSamplerAnisotropy = 1.0F;
};

} // namespace teserra::rhi
