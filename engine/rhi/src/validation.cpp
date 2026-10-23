#include <teserra/rhi/validation.hpp>

#include <teserra/core/assert.hpp>
#include <teserra/rhi/format.hpp>

#include <algorithm>
#include <bit>

namespace teserra::rhi {

namespace {

// Mip levels of a full chain down to 1x1 for the largest dimension. Only the checks use it, which builds
// without assertions compile out.
[[maybe_unused]] u32 fullMipChain(u32 largestDimension) noexcept {
    return static_cast<u32>(std::bit_width(largestDimension));
}

} // namespace

void validateBufferDesc(const BufferDesc& desc) {
    TESERRA_ASSERT(desc.sizeInBytes > 0, "Buffer '{}' has a size of 0", desc.debugName);
    TESERRA_ASSERT(desc.usage != BufferUsage::None, "Buffer '{}' has no usage", desc.debugName);
    TESERRA_ASSERT(desc.memory != MemoryLocation::Readback || hasAll(desc.usage, BufferUsage::CopyDst),
                   "Readback buffer '{}' needs BufferUsage::CopyDst, the only way the GPU writes into it",
                   desc.debugName);
}

void validateWriteBuffer(const BufferDesc& desc, u64 offset, u64 size) {
    TESERRA_ASSERT(hasAll(desc.usage, BufferUsage::CopyDst),
                   "writeBuffer to buffer '{}', created without BufferUsage::CopyDst", desc.debugName);
    TESERRA_ASSERT(desc.memory == MemoryLocation::GpuOnly,
                   "writeBuffer to buffer '{}', which is not GpuOnly; write Upload buffers through map()",
                   desc.debugName);
    TESERRA_ASSERT(offset <= desc.sizeInBytes && size <= desc.sizeInBytes - offset,
                   "writeBuffer of {} bytes at offset {} is outside buffer '{}' ({} bytes)", size, offset,
                   desc.debugName, desc.sizeInBytes);
}

void validateMapBuffer(const BufferDesc& desc) {
    TESERRA_ASSERT(desc.memory != MemoryLocation::GpuOnly,
                   "map() of buffer '{}', which is GpuOnly; only Upload and Readback buffers are mapped",
                   desc.debugName);
}

void validateTextureDesc(const TextureDesc& desc, const DeviceLimits& limits) {
    TESERRA_ASSERT(desc.format != Format::Undefined, "Texture '{}' has no format", desc.debugName);
    TESERRA_ASSERT(desc.width > 0 && desc.height > 0 && desc.depthOrLayers > 0,
                   "Texture '{}' has an empty size {}x{}x{}", desc.debugName, desc.width, desc.height,
                   desc.depthOrLayers);
    TESERRA_ASSERT(desc.width <= limits.maxTextureDimension2D && desc.height <= limits.maxTextureDimension2D,
                   "Texture '{}' is {}x{}, larger than the device's {}", desc.debugName, desc.width,
                   desc.height, limits.maxTextureDimension2D);
    TESERRA_ASSERT(desc.dimension != TextureDimension::Tex2DArray ||
                       desc.depthOrLayers <= limits.maxTextureArrayLayers,
                   "Texture '{}' has {} layers, more than the device's {}", desc.debugName,
                   desc.depthOrLayers, limits.maxTextureArrayLayers);
    TESERRA_ASSERT(desc.dimension != TextureDimension::Cube || desc.depthOrLayers == 6,
                   "Cube texture '{}' has {} faces instead of 6", desc.debugName, desc.depthOrLayers);
    TESERRA_ASSERT(desc.dimension == TextureDimension::Tex3D ||
                       desc.dimension == TextureDimension::Tex2DArray ||
                       desc.dimension == TextureDimension::Cube || desc.depthOrLayers == 1,
                   "2D texture '{}' has a depth of {}", desc.debugName, desc.depthOrLayers);

    const u32 largest = std::max(desc.width, desc.height);
    TESERRA_ASSERT(desc.mipLevels == kAllMips ||
                       (desc.mipLevels > 0 && desc.mipLevels <= fullMipChain(largest)),
                   "Texture '{}' has {} mip levels; {}x{} has at most {}", desc.debugName, desc.mipLevels,
                   desc.width, desc.height, fullMipChain(largest));
    TESERRA_ASSERT(desc.sampleCount == 1 || desc.mipLevels == 1, "Multisampled texture '{}' has mip levels",
                   desc.debugName);

    const bool depth = isDepthStencil(desc.format);
    TESERRA_ASSERT(
        depth == hasAny(desc.usage, TextureUsage::DepthStencil),
        "Texture '{}': TextureUsage::DepthStencil is for depth formats only, and depth formats need it "
        "(format {})",
        desc.debugName, formatInfo(desc.format).name);
    TESERRA_ASSERT(!depth || !hasAny(desc.usage, TextureUsage::RenderTarget | TextureUsage::Storage),
                   "Depth texture '{}' cannot be a color render target or storage texture", desc.debugName);
}

void validateSamplerDesc(const SamplerDesc& desc) {
    TESERRA_ASSERT(desc.maxAnisotropy >= 1.0F, "Sampler '{}' has a maximum anisotropy of {}, below 1",
                   desc.debugName, desc.maxAnisotropy);
}

void validateShaderModuleDesc(const ShaderModuleDesc& desc) {
    TESERRA_ASSERT(!desc.blob.empty(), "Shader module '{}' has no code", desc.debugName);
    TESERRA_ASSERT(!desc.entryPoint.empty(), "Shader module '{}' has no entry point", desc.debugName);
    TESERRA_ASSERT(desc.stage != ShaderStage::Compute,
                   "Shader module '{}' is a compute shader; the RHI has no compute pipelines yet",
                   desc.debugName);
    TESERRA_ASSERT(desc.stage == ShaderStage::Vertex || desc.stage == ShaderStage::Fragment,
                   "Shader module '{}' must have exactly one stage", desc.debugName);
}

void validateGraphicsPipelineDesc(const GraphicsPipelineDesc& desc) {
    TESERRA_ASSERT(!desc.vertexShader.isNull(), "Graphics pipeline '{}' has no vertex shader",
                   desc.debugName);
    TESERRA_ASSERT(desc.vertexBuffers.size() <= kMaxVertexBuffers,
                   "Graphics pipeline '{}' has {} vertex buffers; the limit is {}", desc.debugName,
                   desc.vertexBuffers.size(), kMaxVertexBuffers);
    TESERRA_ASSERT(desc.vertexAttributes.size() <= kMaxVertexAttributes,
                   "Graphics pipeline '{}' has {} vertex attributes; the limit is {}", desc.debugName,
                   desc.vertexAttributes.size(), kMaxVertexAttributes);
    for (usize i = 0; i < desc.vertexAttributes.size(); ++i) {
        const VertexAttribute& attribute = desc.vertexAttributes[i];
        TESERRA_ASSERT(
            attribute.bufferIndex < desc.vertexBuffers.size(),
            "Graphics pipeline '{}': attribute at location {} reads vertex buffer {}, but {} are declared",
            desc.debugName, attribute.location, attribute.bufferIndex, desc.vertexBuffers.size());
        for (usize j = 0; j < i; ++j) {
            TESERRA_ASSERT(desc.vertexAttributes[j].location != attribute.location,
                           "Graphics pipeline '{}' has two attributes at location {}", desc.debugName,
                           attribute.location);
        }
    }

    const RenderTargetLayout& targets = desc.targets;
    bool unusedSlotSeen = false;
    bool anyColor = false;
    for (u32 i = 0; i < kMaxColorAttachments; ++i) {
        const Format format = targets.colorFormats[i];
        if (format == Format::Undefined) {
            unusedSlotSeen = true;
            continue;
        }
        TESERRA_ASSERT(!unusedSlotSeen, "Graphics pipeline '{}': color target {} follows an unused slot",
                       desc.debugName, i);
        TESERRA_ASSERT(!isDepthStencil(format),
                       "Graphics pipeline '{}': color target {} has the depth format {}", desc.debugName, i,
                       formatInfo(format).name);
        anyColor = true;
    }
    TESERRA_ASSERT(targets.depthStencilFormat == Format::Undefined ||
                       isDepthStencil(targets.depthStencilFormat),
                   "Graphics pipeline '{}': the depth-stencil target has the color format {}", desc.debugName,
                   formatInfo(targets.depthStencilFormat).name);
    TESERRA_ASSERT(anyColor || targets.depthStencilFormat != Format::Undefined,
                   "Graphics pipeline '{}' has no render targets", desc.debugName);
    TESERRA_ASSERT(targets.sampleCount >= 1, "Graphics pipeline '{}' has a sample count of 0",
                   desc.debugName);
}

} // namespace teserra::rhi
