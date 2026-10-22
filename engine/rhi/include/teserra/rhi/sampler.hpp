#pragma once

#include <teserra/core/types.hpp>

#include <optional>
#include <string_view>

namespace teserra::rhi {

/// Texel filtering within one mip level.
enum class Filter : u8 {
    Nearest,
    Linear,
};

/// Filtering between mip levels.
enum class MipmapMode : u8 {
    Nearest,
    Linear,
};

/// What sampling outside the 0..1 texture coordinate range reads.
enum class AddressMode : u8 {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
};

/// Comparison of a new value with a stored one: depth tests, shadow samplers.
enum class CompareOp : u8 {
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always,
};

/// Creation parameters of a sampler.
struct SamplerDesc {
    Filter minFilter = Filter::Linear;
    Filter magFilter = Filter::Linear;
    MipmapMode mipmapMode = MipmapMode::Linear;
    AddressMode addressU = AddressMode::ClampToEdge;
    AddressMode addressV = AddressMode::ClampToEdge;
    AddressMode addressW = AddressMode::ClampToEdge;
    /// 1 disables anisotropic filtering; values above DeviceLimits::maxSamplerAnisotropy are clamped.
    f32 maxAnisotropy = 1.0F;
    /// Makes a comparison sampler for shadow maps (future use).
    std::optional<CompareOp> compare{};
    /// Shown by graphics debuggers and in validation messages.
    std::string_view debugName{};
};

} // namespace teserra::rhi
