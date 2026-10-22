#pragma once

// Validation shared by every backend (docs/architecture/rhi.md#validation-layer). Backends call it when
// DeviceDesc::enableValidation is set: the descriptor checks at the start of their create* functions, the
// upload and mapping checks in writeBuffer() and map(), and validateCommands() in submit(). A violation is a
// bug in the caller: it triggers TESERRA_ASSERT with the object's debug name, and does nothing in builds
// without assertions. Whether a handle given to a create* function or an upload is alive is the device's
// check.

#include <teserra/rhi/buffer.hpp>
#include <teserra/rhi/command_list.hpp>
#include <teserra/rhi/handles.hpp>
#include <teserra/rhi/limits.hpp>
#include <teserra/rhi/pipeline.hpp>
#include <teserra/rhi/sampler.hpp>
#include <teserra/rhi/shader.hpp>
#include <teserra/rhi/texture.hpp>

#include <optional>

namespace teserra::rhi {

/// A size above 0, a usage, and Readback memory only for copy destinations.
void validateBufferDesc(const BufferDesc& desc);

/// For Device::writeBuffer(): BufferUsage::CopyDst, GpuOnly memory, and `size` bytes from `offset` inside the
/// buffer.
void validateWriteBuffer(const BufferDesc& desc, u64 offset, u64 size);

/// For Device::map(): Upload or Readback memory.
void validateMapBuffer(const BufferDesc& desc);

/// The objects of a device, as validateCommands() looks them up. A device implements it over its own storage.
/// Each function returns the descriptor of a live object, or std::nullopt for a stale handle or one the
/// device did not create; the views of a returned descriptor (debug name, vertex layouts) stay valid while
/// the object lives.
class ResourceLookup {
public:
    [[nodiscard]] virtual std::optional<BufferDesc> buffer(BufferHandle handle) const = 0;
    [[nodiscard]] virtual std::optional<TextureDesc> texture(TextureHandle handle) const = 0;
    [[nodiscard]] virtual std::optional<GraphicsPipelineDesc>
    graphicsPipeline(GraphicsPipelineHandle handle) const = 0;

protected:
    ResourceLookup() = default;
    ~ResourceLookup() = default; // never owned through this interface
    ResourceLookup(const ResourceLookup&) = default;
    ResourceLookup& operator=(const ResourceLookup&) = default;
    ResourceLookup(ResourceLookup&&) = default;
    ResourceLookup& operator=(ResourceLookup&&) = default;
};

/// Checks a submitted command list against a device's objects: handles are alive; attachments, vertex and
/// index buffers and copies have the usage they need; a bound pipeline declares the formats of the open
/// render pass; copies stay inside their buffers and textures.
void validateCommands(const CommandList& commands, const ResourceLookup& objects);

/// A defined format, a size from 1 to the device's limits, mip levels that fit the size, DepthStencil usage
/// exactly for depth formats, and 6 layers for a cube.
void validateTextureDesc(const TextureDesc& desc, const DeviceLimits& limits);

/// Anisotropy of at least 1.
void validateSamplerDesc(const SamplerDesc& desc);

/// A blob and an entry point.
void validateShaderModuleDesc(const ShaderModuleDesc& desc);

/// A vertex shader, vertex buffers and attributes within the limits, attributes that refer to declared
/// buffers with unique locations, at least one render target, color formats used from slot 0 on without
/// gaps, and a depth-stencil target with a depth format.
void validateGraphicsPipelineDesc(const GraphicsPipelineDesc& desc);

} // namespace teserra::rhi
