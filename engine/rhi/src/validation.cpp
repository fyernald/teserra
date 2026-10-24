#include <teserra/rhi/validation.hpp>

#include <teserra/core/assert.hpp>
#include <teserra/rhi/format.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace teserra::rhi {

namespace {

// For the messages, like the checks.
[[maybe_unused]] std::string_view bindingTypeName(BindingType type) noexcept {
    switch (type) {
        case BindingType::UniformBuffer:
            return "uniform buffer";
        case BindingType::StorageBuffer:
            return "storage buffer";
        case BindingType::SampledTexture:
            return "sampled texture";
    }
    return "unknown binding";
}

[[maybe_unused]] std::string_view stageName(ShaderStage stage) noexcept {
    return stage == ShaderStage::Vertex ? "vertex" : "fragment";
}

[[maybe_unused]] std::string_view textureUsageName(TextureUsage usage) noexcept {
    switch (usage) {
        case TextureUsage::Sampled:
            return "Sampled";
        case TextureUsage::RenderTarget:
            return "RenderTarget";
        case TextureUsage::DepthStencil:
            return "DepthStencil";
        case TextureUsage::Storage:
            return "Storage";
        default:
            return "(several)";
    }
}

// The entry for `binding` in entries sorted by binding, or nullptr.
const BindGroupLayoutEntry* findEntry(std::span<const BindGroupLayoutEntry> entries, u32 binding) noexcept {
    const auto found = std::ranges::find(entries, binding, &BindGroupLayoutEntry::binding);
    return found != entries.end() ? &*found : nullptr;
}

// Checks the resources a shader's reflection lists against the pipeline's layout.
void checkReflection([[maybe_unused]] const GraphicsPipelineDesc& desc, const ShaderModuleInfo& shader,
                     const PipelineLayoutInfo* layout) {
    for (const ShaderBinding& used : shader.reflection->bindings) {
        TESERRA_ASSERT(
            layout != nullptr,
            "Graphics pipeline '{}': {} shader '{}' uses {} '{}' at (group {}, binding {}), but the "
            "pipeline has no layout",
            desc.debugName, stageName(shader.stage), shader.debugName, bindingTypeName(used.type), used.name,
            used.group, used.binding);
        if (layout == nullptr) {
            return;
        }
        const BindGroupLayoutEntry* entry =
            used.group < kMaxBindGroups ? findEntry(layout->groups[used.group], used.binding) : nullptr;
        TESERRA_ASSERT(entry != nullptr,
                       "Graphics pipeline '{}': {} shader '{}' uses {} '{}' at (group {}, binding {}), which "
                       "layout '{}' does not have",
                       desc.debugName, stageName(shader.stage), shader.debugName, bindingTypeName(used.type),
                       used.name, used.group, used.binding, layout->debugName);
        if (entry == nullptr) {
            continue;
        }
        TESERRA_ASSERT(
            entry->type == used.type,
            "Graphics pipeline '{}': {} shader '{}' uses '{}' at (group {}, binding {}) as a {}, but "
            "layout '{}' has a {} there",
            desc.debugName, stageName(shader.stage), shader.debugName, used.name, used.group, used.binding,
            bindingTypeName(used.type), layout->debugName, bindingTypeName(entry->type));
        TESERRA_ASSERT(
            hasAll(entry->visibility, shader.stage),
            "Graphics pipeline '{}': {} shader '{}' uses '{}' at (group {}, binding {}), which layout "
            "'{}' does not make visible to the {} stage",
            desc.debugName, stageName(shader.stage), shader.debugName, used.name, used.group, used.binding,
            layout->debugName, stageName(shader.stage));
    }
}

} // namespace

StoredBindGroupLayout StoredBindGroupLayout::from(const BindGroupLayoutDesc& desc) {
    StoredBindGroupLayout stored{.entries = {desc.entries.begin(), desc.entries.end()},
                                 .debugName = std::string(desc.debugName)};
    std::ranges::sort(stored.entries, {}, &BindGroupLayoutEntry::binding);
    return stored;
}

StoredPipelineLayout
StoredPipelineLayout::from(std::string_view debugName,
                           const std::array<const StoredBindGroupLayout*, kMaxBindGroups>& layouts) {
    StoredPipelineLayout stored{.groups = {}, .debugName = std::string(debugName)};
    for (usize set = 0; set < kMaxBindGroups; ++set) {
        if (layouts[set] != nullptr) {
            stored.groups[set] = layouts[set]->entries;
        }
    }
    return stored;
}

PipelineLayoutInfo StoredPipelineLayout::info() const noexcept {
    PipelineLayoutInfo result{.groups = {}, .debugName = debugName};
    for (usize set = 0; set < kMaxBindGroups; ++set) {
        result.groups[set] = groups[set];
    }
    return result;
}

StoredBindGroup StoredBindGroup::from(const BindGroupDesc& desc, const StoredBindGroupLayout* layout) {
    StoredBindGroup stored{.layoutEntries =
                               layout != nullptr ? layout->entries : std::vector<BindGroupLayoutEntry>{},
                           .entries = {desc.entries.begin(), desc.entries.end()},
                           .debugName = std::string(desc.debugName)};
    std::ranges::sort(stored.entries, {}, &BindGroupEntry::binding);
    return stored;
}

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

    // The format support table: each usage needs its capability.
    constexpr std::array<std::pair<TextureUsage, FormatCapabilities>, 4> kNeeds{{
        {TextureUsage::Sampled, FormatCapabilities::Sampled},
        {TextureUsage::RenderTarget, FormatCapabilities::RenderTarget},
        {TextureUsage::DepthStencil, FormatCapabilities::DepthStencil},
        {TextureUsage::Storage, FormatCapabilities::Storage},
    }};
    for ([[maybe_unused]] const auto& [usage, capability] : kNeeds) {
        TESERRA_ASSERT(!hasAny(desc.usage, usage) || formatSupports(desc.format, capability),
                       "Texture '{}': format {} does not support TextureUsage::{}", desc.debugName,
                       formatInfo(desc.format).name, textureUsageName(usage));
    }
}

void validateTextureRegion(const TextureDesc& desc, const TextureRegion& region,
                           [[maybe_unused]] std::string_view use) {
    TESERRA_ASSERT(region.mipLevel < mipLevelCount(desc),
                   "{} of texture '{}' at mip {}; it has {} mip levels", use, desc.debugName, region.mipLevel,
                   mipLevelCount(desc));
    [[maybe_unused]] const u32 width = std::max(desc.width >> region.mipLevel, 1U);
    [[maybe_unused]] const u32 height = std::max(desc.height >> region.mipLevel, 1U);
    TESERRA_ASSERT(region.width > 0 && region.height > 0 && region.layerCount > 0,
                   "{} of texture '{}' has an empty region {}x{}, {} layers", use, desc.debugName,
                   region.width, region.height, region.layerCount);
    TESERRA_ASSERT(
        u64{region.x} + region.width <= width && u64{region.y} + region.height <= height &&
            u64{region.arrayLayer} + region.layerCount <= desc.depthOrLayers,
        "{} region ({}, {}) {}x{}, layers {}+{} is outside texture '{}' ({}x{} at mip {}, {} layers)", use,
        region.x, region.y, region.width, region.height, region.arrayLayer, region.layerCount, desc.debugName,
        width, height, region.mipLevel, desc.depthOrLayers);
}

void validateWriteTexture(const TextureDesc& desc, const TextureRegion& region, usize dataSize,
                          u32 bytesPerRow) {
    TESERRA_ASSERT(hasAll(desc.usage, TextureUsage::CopyDst),
                   "writeTexture to texture '{}', created without TextureUsage::CopyDst", desc.debugName);
    TESERRA_ASSERT(!isDepthStencil(desc.format),
                   "writeTexture to depth texture '{}'; depth textures cannot be uploaded in v1",
                   desc.debugName);
    validateTextureRegion(desc, region, "writeTexture");
    [[maybe_unused]] const u32 texelSize = formatInfo(desc.format).bytesPerTexel;
    TESERRA_ASSERT(
        bytesPerRow == 0 || (bytesPerRow >= u64{region.width} * texelSize && bytesPerRow % texelSize == 0),
        "writeTexture to texture '{}': {} bytes per row for {} texels of {} bytes; it must hold a row "
        "and be a multiple of the texel size",
        desc.debugName, bytesPerRow, region.width, texelSize);
    TESERRA_ASSERT(dataSize >= textureDataSize(desc.format, region, bytesPerRow),
                   "writeTexture to texture '{}' needs {} bytes for {}x{}, {} layers, and got {}",
                   desc.debugName, textureDataSize(desc.format, region, bytesPerRow), region.width,
                   region.height, region.layerCount, dataSize);
}

void validateGenerateMipmaps(const TextureDesc& desc) {
    TESERRA_ASSERT(mipLevelCount(desc) > 1, "generateMipmaps of texture '{}', which has one mip level",
                   desc.debugName);
    TESERRA_ASSERT(hasAll(desc.usage, TextureUsage::CopyDst),
                   "generateMipmaps of texture '{}', created without TextureUsage::CopyDst", desc.debugName);
    TESERRA_ASSERT(
        formatSupports(desc.format, FormatCapabilities::Filterable | FormatCapabilities::RenderTarget),
        "generateMipmaps of texture '{}': format {} is not filterable and renderable", desc.debugName,
        formatInfo(desc.format).name);
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

void validateBindGroupLayoutDesc(const BindGroupLayoutDesc& desc) {
    TESERRA_ASSERT(desc.entries.size() <= kMaxBindingsPerGroup,
                   "Bind group layout '{}' has {} entries; the limit is {}", desc.debugName,
                   desc.entries.size(), kMaxBindingsPerGroup);
    for (usize i = 0; i < desc.entries.size(); ++i) {
        const BindGroupLayoutEntry& entry = desc.entries[i];
        TESERRA_ASSERT(entry.binding < kMaxBindingsPerGroup,
                       "Bind group layout '{}' has binding {}; bindings are 0 to {}", desc.debugName,
                       entry.binding, kMaxBindingsPerGroup - 1);
        TESERRA_ASSERT(
            entry.visibility != ShaderStage::None &&
                !hasAny(entry.visibility, ~(ShaderStage::Vertex | ShaderStage::Fragment)),
            "Bind group layout '{}': binding {} must be visible to the vertex and/or fragment stage",
            desc.debugName, entry.binding);
        TESERRA_ASSERT(
            !entry.hasDynamicOffset || entry.type == BindingType::UniformBuffer,
            "Bind group layout '{}': binding {} is a {} with a dynamic offset; only uniform buffers "
            "have them",
            desc.debugName, entry.binding, bindingTypeName(entry.type));
        for (usize j = 0; j < i; ++j) {
            TESERRA_ASSERT(desc.entries[j].binding != entry.binding,
                           "Bind group layout '{}' has two entries for binding {}", desc.debugName,
                           entry.binding);
        }
    }
}

void validatePipelineLayoutDesc(const PipelineLayoutDesc& desc, const ResourceLookup& objects) {
    TESERRA_ASSERT(desc.groups.size() <= kMaxBindGroups,
                   "Pipeline layout '{}' has {} groups; the limit is {}", desc.debugName, desc.groups.size(),
                   kMaxBindGroups);
    for (usize set = 0; set < desc.groups.size(); ++set) {
        TESERRA_ASSERT(desc.groups[set].isNull() || objects.bindGroupLayout(desc.groups[set]).has_value(),
                       "Pipeline layout '{}': the layout of set {} ({}) is not alive", desc.debugName, set,
                       desc.groups[set]);
    }
}

void validateBindGroupDesc(const BindGroupDesc& desc, const ResourceLookup& objects,
                           [[maybe_unused]] const DeviceLimits& limits) {
    const std::optional<BindGroupLayoutDesc> layout =
        desc.layout.isNull() ? std::nullopt : objects.bindGroupLayout(desc.layout);
    TESERRA_ASSERT(layout.has_value(), "Bind group '{}': its layout {} is not alive", desc.debugName,
                   desc.layout);
    if (!layout) {
        return;
    }
    TESERRA_ASSERT(desc.entries.size() == layout->entries.size(),
                   "Bind group '{}' has {} entries for the {} binding(s) of layout '{}'", desc.debugName,
                   desc.entries.size(), layout->entries.size(), layout->debugName);
    for (usize i = 0; i < desc.entries.size(); ++i) {
        const BindGroupEntry& entry = desc.entries[i];
        const BindGroupLayoutEntry* binding = findEntry(layout->entries, entry.binding);
        TESERRA_ASSERT(binding != nullptr,
                       "Bind group '{}' has an entry for binding {}, which layout '{}' lacks", desc.debugName,
                       entry.binding, layout->debugName);
        for (usize j = 0; j < i; ++j) {
            TESERRA_ASSERT(desc.entries[j].binding != entry.binding,
                           "Bind group '{}' has two entries for binding {}", desc.debugName, entry.binding);
        }
        if (binding == nullptr) {
            continue;
        }
        if (binding->type == BindingType::SampledTexture) {
            const std::optional<TextureDesc> texture =
                entry.texture.isNull() ? std::nullopt : objects.texture(entry.texture);
            TESERRA_ASSERT(texture.has_value(), "Bind group '{}': binding {} needs a live texture, not {}",
                           desc.debugName, entry.binding, entry.texture);
            TESERRA_ASSERT(
                !texture || hasAll(texture->usage, TextureUsage::Sampled),
                "Bind group '{}': texture '{}' at binding {} was created without TextureUsage::Sampled",
                desc.debugName, texture->debugName, entry.binding);
            TESERRA_ASSERT(!entry.sampler.isNull() && objects.sampler(entry.sampler).has_value(),
                           "Bind group '{}': binding {} needs a live sampler, not {}", desc.debugName,
                           entry.binding, entry.sampler);
            continue;
        }
        const BufferBinding& range = entry.buffer;
        const std::optional<BufferDesc> buffer =
            range.buffer.isNull() ? std::nullopt : objects.buffer(range.buffer);
        TESERRA_ASSERT(buffer.has_value(), "Bind group '{}': binding {} needs a live buffer, not {}",
                       desc.debugName, entry.binding, range.buffer);
        if (!buffer) {
            continue;
        }
        [[maybe_unused]] const BufferUsage usage =
            binding->type == BindingType::UniformBuffer ? BufferUsage::Uniform : BufferUsage::Storage;
        TESERRA_ASSERT(hasAll(buffer->usage, usage),
                       "Bind group '{}': buffer '{}' at binding {} was created without BufferUsage::{}",
                       desc.debugName, buffer->debugName, entry.binding,
                       usage == BufferUsage::Uniform ? "Uniform" : "Storage");
        TESERRA_ASSERT(
            range.offset < buffer->sizeInBytes && range.size <= buffer->sizeInBytes - range.offset,
            "Bind group '{}': binding {} sees {} bytes from offset {} of buffer '{}', which has {}",
            desc.debugName, entry.binding, range.size, range.offset, buffer->debugName, buffer->sizeInBytes);
        TESERRA_ASSERT(!binding->hasDynamicOffset || range.size != 0,
                       "Bind group '{}': binding {} has a dynamic offset, so its buffer range needs a size",
                       desc.debugName, entry.binding);
        if (binding->type != BindingType::UniformBuffer) {
            continue;
        }
        TESERRA_ASSERT(
            range.offset % limits.uniformBufferOffsetAlignment == 0,
            "Bind group '{}': binding {} starts at offset {} of buffer '{}', which is not a multiple "
            "of the device's uniformBufferOffsetAlignment ({})",
            desc.debugName, entry.binding, range.offset, buffer->debugName,
            limits.uniformBufferOffsetAlignment);
        [[maybe_unused]] const u64 seen = range.size != 0 ? range.size : buffer->sizeInBytes - range.offset;
        TESERRA_ASSERT(seen <= limits.maxUniformBufferRange,
                       "Bind group '{}': binding {} sees {} bytes of buffer '{}', more than the device's "
                       "maxUniformBufferRange ({})",
                       desc.debugName, entry.binding, seen, buffer->debugName, limits.maxUniformBufferRange);
    }
}

void validateGraphicsPipelineResources(const GraphicsPipelineDesc& desc, const ResourceLookup& objects) {
    std::optional<PipelineLayoutInfo> layout;
    if (!desc.layout.isNull()) {
        layout = objects.pipelineLayout(desc.layout);
        TESERRA_ASSERT(layout.has_value(), "Graphics pipeline '{}': its layout {} is not alive",
                       desc.debugName, desc.layout);
        if (!layout) {
            return;
        }
    }
    for (const ShaderModuleHandle handle : {desc.vertexShader, desc.fragmentShader}) {
        const std::optional<ShaderModuleInfo> shader =
            handle.isNull() ? std::nullopt : objects.shaderModule(handle);
        if (shader) {
            checkReflection(desc, *shader, layout ? &*layout : nullptr);
        }
    }
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
        TESERRA_ASSERT(isDepthStencil(format) || formatSupports(format, FormatCapabilities::RenderTarget),
                       "Graphics pipeline '{}': color target {} has format {}, which cannot be rendered to",
                       desc.debugName, i, formatInfo(format).name);
        TESERRA_ASSERT(!desc.colorBlend[i].enabled || formatSupports(format, FormatCapabilities::Blendable),
                       "Graphics pipeline '{}': color target {} blends format {}, which cannot be blended",
                       desc.debugName, i, formatInfo(format).name);
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
