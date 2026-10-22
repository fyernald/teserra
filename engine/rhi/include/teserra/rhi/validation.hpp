#pragma once

// Descriptor validation shared by every backend (docs/architecture/rhi.md#validation-layer). Backends call
// these at the start of their create* functions when DeviceDesc::enableValidation is set. A violation is a
// bug in the caller: it triggers TESERRA_ASSERT with the descriptor's debug name, and does nothing in builds
// without assertions. Checks that need the device's objects, such as whether a shader handle is alive, are
// the device's.

#include <teserra/rhi/buffer.hpp>
#include <teserra/rhi/limits.hpp>
#include <teserra/rhi/pipeline.hpp>
#include <teserra/rhi/sampler.hpp>
#include <teserra/rhi/shader.hpp>
#include <teserra/rhi/texture.hpp>

namespace teserra::rhi {

/// A size above 0, a usage, and Readback memory only for copy destinations.
void validateBufferDesc(const BufferDesc& desc);

/// For Device::writeBuffer(): BufferUsage::CopyDst, GpuOnly memory, and `size` bytes from `offset` inside the
/// buffer.
void validateWriteBuffer(const BufferDesc& desc, u64 offset, u64 size);

/// For Device::map(): Upload or Readback memory.
void validateMapBuffer(const BufferDesc& desc);

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
