#pragma once

// .tshader blobs: one compiled shader stage with its SPIR-V, GLSL for OpenGL and reflection (ADR-0017,
// docs/architecture/rhi.md#tshader-blobs). teserra_shaderc writes them with writeShaderBlob() at build time;
// backends read them with readShaderBlob(). Because the tool links teserra_rhi for this, teserra_rhi itself
// cannot compile shaders with teserra_add_shaders().

#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/rhi/binding.hpp>
#include <teserra/rhi/limits.hpp>
#include <teserra/rhi/shader.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace teserra::rhi {

/// The blob format version this build writes and reads.
inline constexpr u32 kShaderBlobVersion = 1;

/// The single binding index that backends without bind groups (OpenGL) use for (group, binding). The GLSL
/// in a blob is emitted with these indices, and the OpenGL backend binds resources at them, so both always
/// agree.
[[nodiscard]] constexpr u32 flatBinding(u32 group, u32 binding) noexcept {
    return group * kMaxBindingsPerGroup + binding;
}

/// The component type of a vertex input.
enum class ShaderScalarType : u8 {
    Float,
    Int,
    Uint,
};

/// A resource the shader uses.
struct ShaderBinding {
    u32 group = 0;
    u32 binding = 0;
    BindingType type = BindingType::UniformBuffer;
    /// Uniform buffers: the std140 size rounded up to 16 bytes, the smallest range that may be bound.
    /// Storage buffers: the size without a runtime-sized array. Textures: 0.
    u32 size = 0;
    /// The block name of a buffer, the variable name of a texture.
    std::string name{};

    friend bool operator==(const ShaderBinding&, const ShaderBinding&) = default;
};

/// An input of a vertex shader.
struct ShaderVertexInput {
    u32 location = 0;
    ShaderScalarType scalarType = ShaderScalarType::Float;
    /// 1 to 4: float is 1, vec4 is 4.
    u32 components = 1;
    std::string name{};

    friend bool operator==(const ShaderVertexInput&, const ShaderVertexInput&) = default;
};

/// What a shader stage uses, for validating pipeline and bind group layouts against it.
struct ShaderReflection {
    /// Sorted by (group, binding).
    std::vector<ShaderBinding> bindings{};
    /// Sorted by location; empty except for vertex shaders.
    std::vector<ShaderVertexInput> vertexInputs{};
    /// Size of the push-constant block; 0 in v1, which has no push constants.
    u32 pushConstantSize = 0;

    friend bool operator==(const ShaderReflection&, const ShaderReflection&) = default;
};

/// The parts of a blob. From readShaderBlob(), `spirv` and `glsl` point into the blob.
struct ShaderBlobContents {
    ShaderStage stage = ShaderStage::Vertex;
    /// SPIR-V for Vulkan 1.2, with its descriptor set and binding decorations. A whole number of words.
    std::span<const std::byte> spirv{};
    /// GLSL 450 core for OpenGL 4.5, with `layout(binding = flatBinding(set, binding))`.
    std::string_view glsl{};
    ShaderReflection reflection{};
};

/// Serializes `contents` in format version kShaderBlobVersion.
[[nodiscard]] std::vector<std::byte> writeShaderBlob(const ShaderBlobContents& contents);

/// Reads a blob made by writeShaderBlob(). Fails with ErrorCode::InvalidData when the blob is not a .tshader
/// blob or is truncated or inconsistent, and with ErrorCode::Unsupported when it has another format version.
/// Never reads outside `blob`.
[[nodiscard]] Result<ShaderBlobContents> readShaderBlob(std::span<const std::byte> blob);

/// The blob of a shader module, as every backend's createShaderModule() reads it: readShaderBlob(desc.blob)
/// with its errors prefixed by "Shader module '<debugName>': ", and ErrorCode::InvalidArgument when the blob
/// holds another stage than desc.stage.
[[nodiscard]] Result<ShaderBlobContents> readShaderModuleBlob(const ShaderModuleDesc& desc);

} // namespace teserra::rhi
