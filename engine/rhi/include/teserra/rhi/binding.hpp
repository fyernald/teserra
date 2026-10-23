#pragma once

// Resource bindings: shaders declare resources at (group, binding), `layout(set = S, binding = B)` in GLSL
// (docs/architecture/rhi.md#binding-model). A bind group layout is the shape of one group, a pipeline layout
// the groups of a pipeline by set index, and a bind group the concrete resources of one group. Creation
// copies what it needs from the layouts it names, so a layout may be destroyed while objects made from it
// live.

#include <teserra/core/types.hpp>
#include <teserra/rhi/handles.hpp>
#include <teserra/rhi/shader.hpp>

#include <span>
#include <string_view>

namespace teserra::rhi {

// Set indices by update frequency, the convention of the renderer and every engine shader. Grouping
// resources by how often they change lets a frame rebind only the groups that did.

/// Per frame or per view: camera matrices, time, viewport size.
inline constexpr u32 kFrameGroup = 0;
/// Per pass: pass-specific inputs, such as a previous pass's output.
inline constexpr u32 kPassGroup = 1;
/// Per material: textures and the material parameter buffer.
inline constexpr u32 kMaterialGroup = 2;
/// Per draw: per-object data, through a dynamic offset into a ring buffer.
inline constexpr u32 kDrawGroup = 3;

/// What a binding holds. These are the v1 types; textures are always combined with a sampler.
enum class BindingType : u8 {
    UniformBuffer,
    StorageBuffer, ///< read-only in v1
    SampledTexture,
};

/// One binding of a bind group layout.
struct BindGroupLayoutEntry {
    /// The shader's `binding = B`, below kMaxBindingsPerGroup.
    u32 binding = 0;
    BindingType type = BindingType::UniformBuffer;
    /// The stages that see the binding: Vertex, Fragment or both.
    ShaderStage visibility = ShaderStage::Vertex | ShaderStage::Fragment;
    /// Uniform buffers only: bindGroup() passes an offset for this binding, added to the bind group's.
    bool hasDynamicOffset = false;

    friend bool operator==(const BindGroupLayoutEntry&, const BindGroupLayoutEntry&) = default;
};

/// Creation parameters of a bind group layout.
struct BindGroupLayoutDesc {
    /// At most kMaxBindingsPerGroup, with unique binding numbers, in any order. Only read during creation.
    std::span<const BindGroupLayoutEntry> entries{};
    /// Shown in validation messages.
    std::string_view debugName{};
};

/// Creation parameters of a pipeline layout.
struct PipelineLayoutDesc {
    /// The layout of each group, indexed by set, at most kMaxBindGroups. A null handle leaves its set empty,
    /// so a pipeline can use sets 0 and 2 without 1. Only read during creation.
    std::span<const BindGroupLayoutHandle> groups{};
    /// Shown in validation messages.
    std::string_view debugName{};
};

/// A range of a buffer.
struct BufferBinding {
    BufferHandle buffer{};
    u64 offset = 0;
    /// Bytes from `offset`; 0 means to the end of the buffer. Bindings with a dynamic offset need a size,
    /// the window that the dynamic offset moves through the buffer.
    u64 size = 0;
};

/// The resource of one binding of a bind group.
struct BindGroupEntry {
    /// A binding number of the group's layout.
    u32 binding = 0;
    /// UniformBuffer and StorageBuffer bindings.
    BufferBinding buffer{};
    /// SampledTexture bindings: the texture and the sampler it is read with.
    TextureHandle texture{};
    SamplerHandle sampler{};
};

/// Creation parameters of a bind group.
struct BindGroupDesc {
    BindGroupLayoutHandle layout{};
    /// One per binding of the layout, in any order. Only read during creation.
    std::span<const BindGroupEntry> entries{};
    /// Shown in validation messages.
    std::string_view debugName{};
};

} // namespace teserra::rhi
