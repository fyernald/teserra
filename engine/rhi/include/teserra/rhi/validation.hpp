#pragma once

// Validation shared by every backend (docs/architecture/rhi.md#validation-layer). Backends call it when
// DeviceDesc::enableValidation is set: the descriptor checks at the start of their create* functions, the
// upload and mapping checks in writeBuffer() and map(), and validateCommands() in submit(). A violation is a
// bug in the caller: it triggers TESERRA_ASSERT with the object's debug name, and does nothing in builds
// without assertions. Whether a handle given to a create* function or an upload is alive is the device's
// check.

#include <teserra/rhi/binding.hpp>
#include <teserra/rhi/buffer.hpp>
#include <teserra/rhi/command_list.hpp>
#include <teserra/rhi/handles.hpp>
#include <teserra/rhi/limits.hpp>
#include <teserra/rhi/pipeline.hpp>
#include <teserra/rhi/sampler.hpp>
#include <teserra/rhi/shader.hpp>
#include <teserra/rhi/shader_blob.hpp>
#include <teserra/rhi/texture.hpp>

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace teserra::rhi {

/// A size above 0, a usage, and Readback memory only for copy destinations.
void validateBufferDesc(const BufferDesc& desc);

/// For Device::writeBuffer(): BufferUsage::CopyDst, GpuOnly memory, and `size` bytes from `offset` inside the
/// buffer.
void validateWriteBuffer(const BufferDesc& desc, u64 offset, u64 size);

/// For Device::map(): Upload or Readback memory.
void validateMapBuffer(const BufferDesc& desc);

/// A region of `desc` that is not empty and lies inside one of its mip levels and its layers. `use` names
/// the operation in the message, e.g. "writeTexture".
void validateTextureRegion(const TextureDesc& desc, const TextureRegion& region, std::string_view use);

/// For Device::writeTexture(): TextureUsage::CopyDst, a color format (depth textures cannot be uploaded in
/// v1), a valid region, `bytesPerRow` 0 or at least one row and a multiple of the texel size, and at least
/// textureDataSize() bytes of data.
void validateWriteTexture(const TextureDesc& desc, const TextureRegion& region, usize dataSize,
                          u32 bytesPerRow);

/// For Device::generateMipmaps(): more than one mip level, TextureUsage::CopyDst, and a format that is
/// filterable and renderable.
void validateGenerateMipmaps(const TextureDesc& desc);

/// The bind groups of a pipeline layout as a device keeps them: per set, the entries its bind group layout
/// had when the pipeline layout was created, sorted by binding. A set without a layout has no entries.
struct PipelineLayoutInfo {
    std::array<std::span<const BindGroupLayoutEntry>, kMaxBindGroups> groups{};
    std::string_view debugName{};
};

/// A bind group as a device keeps it.
struct BindGroupInfo {
    /// The entries its layout had when the group was created, sorted by binding.
    std::span<const BindGroupLayoutEntry> layoutEntries{};
    /// Its resources, sorted by binding like layoutEntries.
    std::span<const BindGroupEntry> entries{};
    std::string_view debugName{};
};

/// A shader module as validation needs it.
struct ShaderModuleInfo {
    ShaderStage stage = ShaderStage::Vertex;
    /// The reflection of the module's .tshader blob; never null.
    const ShaderReflection* reflection = nullptr;
    std::string_view debugName{};
};

/// Copies of the entries a device keeps for a bind group layout, a pipeline layout or a pipeline, which
/// outlive the layouts they came from. Backends store one per object and hand out its info() views.
struct StoredBindGroupLayout {
    /// Sorted by binding.
    std::vector<BindGroupLayoutEntry> entries;
    std::string debugName;

    [[nodiscard]] static StoredBindGroupLayout from(const BindGroupLayoutDesc& desc);
    [[nodiscard]] BindGroupLayoutDesc desc() const noexcept {
        return {.entries = entries, .debugName = debugName};
    }
};

/// The copied group entries of a pipeline layout; also kept by every pipeline created with it.
struct StoredPipelineLayout {
    std::array<std::vector<BindGroupLayoutEntry>, kMaxBindGroups> groups;
    std::string debugName;

    /// `layouts` holds the live layout of each set of the descriptor, or null for a null or stale handle,
    /// which leaves that set empty (validation reports a stale one before).
    [[nodiscard]] static StoredPipelineLayout
    from(std::string_view debugName, const std::array<const StoredBindGroupLayout*, kMaxBindGroups>& layouts);
    [[nodiscard]] PipelineLayoutInfo info() const noexcept;
};

/// The copied layout entries and the resources of a bind group.
struct StoredBindGroup {
    std::vector<BindGroupLayoutEntry> layoutEntries;
    /// Sorted by binding.
    std::vector<BindGroupEntry> entries;
    std::string debugName;

    /// `layout` is the group's live layout, or null for a stale one (validation reports it before).
    [[nodiscard]] static StoredBindGroup from(const BindGroupDesc& desc, const StoredBindGroupLayout* layout);
    [[nodiscard]] BindGroupInfo info() const noexcept {
        return {.layoutEntries = layoutEntries, .entries = entries, .debugName = debugName};
    }
};

/// The objects of a device, as validation looks them up. A device implements it over its own storage.
/// Each function returns a view of a live object, or std::nullopt for a stale handle or one the device did
/// not create; the views of a returned value (debug name, vertex layouts, entries) stay valid until the
/// device creates or destroys objects, which validation never does.
class ResourceLookup {
public:
    [[nodiscard]] virtual std::optional<BufferDesc> buffer(BufferHandle handle) const = 0;
    [[nodiscard]] virtual std::optional<TextureDesc> texture(TextureHandle handle) const = 0;
    [[nodiscard]] virtual std::optional<SamplerDesc> sampler(SamplerHandle handle) const = 0;
    [[nodiscard]] virtual std::optional<ShaderModuleInfo> shaderModule(ShaderModuleHandle handle) const = 0;
    /// The layout's entries sorted by binding.
    [[nodiscard]] virtual std::optional<BindGroupLayoutDesc>
    bindGroupLayout(BindGroupLayoutHandle handle) const = 0;
    [[nodiscard]] virtual std::optional<PipelineLayoutInfo>
    pipelineLayout(PipelineLayoutHandle handle) const = 0;
    [[nodiscard]] virtual std::optional<BindGroupInfo> bindGroup(BindGroupHandle handle) const = 0;
    [[nodiscard]] virtual std::optional<GraphicsPipelineDesc>
    graphicsPipeline(GraphicsPipelineHandle handle) const = 0;
    /// The groups of the layout a live pipeline was created with; no groups for a pipeline without one.
    [[nodiscard]] virtual std::optional<PipelineLayoutInfo>
    graphicsPipelineLayout(GraphicsPipelineHandle handle) const = 0;

protected:
    ResourceLookup() = default;
    ~ResourceLookup() = default; // never owned through this interface
    ResourceLookup(const ResourceLookup&) = default;
    ResourceLookup& operator=(const ResourceLookup&) = default;
    ResourceLookup(ResourceLookup&&) = default;
    ResourceLookup& operator=(ResourceLookup&&) = default;
};

/// Checks a submitted command list against a device's objects and limits: handles are alive; attachments,
/// vertex and index buffers and copies have the usage they need; a bound pipeline declares the formats of
/// the open render pass; draws have every vertex buffer slot of the pipeline bound, and at every set of its
/// layout a bind group with an equal layout; bind groups get one dynamic offset per dynamic binding, each a
/// multiple of the uniform buffer offset alignment that keeps the binding's window inside its buffer, and
/// their resources are alive; copies stay inside their buffers and textures.
void validateCommands(const CommandList& commands, const ResourceLookup& objects, const DeviceLimits& limits);

/// A defined format, a size from 1 to the device's limits, mip levels that fit the size, DepthStencil usage
/// exactly for depth formats, 6 layers for a cube, and usages that the format support table allows.
void validateTextureDesc(const TextureDesc& desc, const DeviceLimits& limits);

/// Anisotropy of at least 1.
void validateSamplerDesc(const SamplerDesc& desc);

/// A blob and an entry point.
void validateShaderModuleDesc(const ShaderModuleDesc& desc);

/// At most kMaxBindingsPerGroup entries with unique binding numbers below that limit, each visible to the
/// vertex and/or fragment stage, and dynamic offsets only on uniform buffers.
void validateBindGroupLayoutDesc(const BindGroupLayoutDesc& desc);

/// At most kMaxBindGroups groups, each null or a live layout.
void validatePipelineLayoutDesc(const PipelineLayoutDesc& desc, const ResourceLookup& objects);

/// A live layout and exactly one entry per binding of it, each with a live resource of the binding's type:
/// a buffer with Uniform or Storage usage and a range inside it (with a size if the binding has a dynamic
/// offset), or a sampled texture and a sampler. Uniform buffer ranges start at a multiple of the device's
/// uniformBufferOffsetAlignment and see at most its maxUniformBufferRange bytes.
void validateBindGroupDesc(const BindGroupDesc& desc, const ResourceLookup& objects,
                           const DeviceLimits& limits);

/// A vertex shader, vertex buffers and attributes within the limits, attributes that refer to declared
/// buffers with unique locations, at least one render target, color formats used from slot 0 on without
/// gaps that can be rendered to (and blended, where blending is on), and a depth-stencil target with a depth
/// format.
void validateGraphicsPipelineDesc(const GraphicsPipelineDesc& desc);

/// The pipeline against the device's objects: a null or live layout, and every resource that the shaders'
/// reflection lists is in that layout at its (group, binding), with the same type, visible to the stage
/// that uses it. Shaders that are not alive are left to the device.
void validateGraphicsPipelineResources(const GraphicsPipelineDesc& desc, const ResourceLookup& objects);

} // namespace teserra::rhi
