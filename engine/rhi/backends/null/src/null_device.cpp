#include <teserra/rhi_null/null_device.hpp>

#include <teserra/core/assert.hpp>
#include <teserra/platform/window.hpp>
#include <teserra/rhi/format.hpp>
#include <teserra/rhi/validation.hpp>

#include <algorithm>
#include <string_view>
#include <type_traits>

namespace teserra::rhi::null {

namespace {

Result<std::unique_ptr<Device>> createNullDevice(const DeviceDesc& desc, platform::Window* window) {
    return std::make_unique<NullDevice>(desc, window);
}

// The pointer behind a pool entry, or nullptr.
template <typename Pool, typename Handle>
auto* find(const Pool& pool, Handle handle) noexcept {
    const auto* object = pool.tryGet(handle);
    return object != nullptr ? &(*object)->desc : nullptr;
}

// Names of the usages the validation requires, one flag at a time, for its messages.
[[maybe_unused]] std::string_view usageName(BufferUsage usage) noexcept {
    switch (usage) {
        case BufferUsage::Vertex:
            return "BufferUsage::Vertex";
        case BufferUsage::Index:
            return "BufferUsage::Index";
        case BufferUsage::CopySrc:
            return "BufferUsage::CopySrc";
        case BufferUsage::CopyDst:
            return "BufferUsage::CopyDst";
        default:
            return "the required usage";
    }
}

[[maybe_unused]] std::string_view usageName(TextureUsage usage) noexcept {
    switch (usage) {
        case TextureUsage::RenderTarget:
            return "TextureUsage::RenderTarget";
        case TextureUsage::DepthStencil:
            return "TextureUsage::DepthStencil";
        case TextureUsage::CopySrc:
            return "TextureUsage::CopySrc";
        case TextureUsage::CopyDst:
            return "TextureUsage::CopyDst";
        default:
            return "the required usage";
    }
}

// Bytes a copy between `copy`'s buffer and a texture of `format` reads or writes in the buffer. Only the
// checks use it, which builds without assertions compile out.
[[maybe_unused]] u64 bufferBytes(const BufferTextureCopy& copy, Format format) {
    const u64 rowBytes = u64{copy.width} * formatInfo(format).bytesPerTexel;
    const u64 pitch = copy.bytesPerRow != 0 ? copy.bytesPerRow : rowBytes;
    return (pitch * ((u64{copy.height} * copy.layerCount) - 1)) + rowBytes;
}

} // namespace

BackendFactory backendFactory() noexcept {
    return {.backend = Backend::Null, .create = &createNullDevice};
}

std::unique_ptr<NullDevice> createHeadlessDevice(const DeviceDesc& desc) {
    return std::make_unique<NullDevice>(desc, nullptr);
}

// ---------------------------------------------------------------------------------------------------------
// Swapchain

NullSwapchain::NullSwapchain(NullDevice& device, const SwapchainDesc& desc, UVec2 framebufferSize)
    : m_device(device), m_format(desc.format), m_size(framebufferSize) {
    createImage();
}

NullSwapchain::~NullSwapchain() {
    releaseImage();
}

void NullSwapchain::present() {
    TESERRA_ASSERT(!m_image.isNull(), "present() without an image; skip presenting while acquireNextImage() "
                                      "returns a null handle");
    if (!m_image.isNull()) {
        ++m_presentCount;
    }
}

void NullSwapchain::resize(UVec2 framebufferSize) {
    if (framebufferSize == m_size) {
        return;
    }
    releaseImage();
    m_size = framebufferSize;
    createImage();
}

void NullSwapchain::createImage() {
    if (m_size.x != 0 && m_size.y != 0) {
        m_image = m_device.createSwapchainImage(m_format, m_size);
    }
}

void NullSwapchain::releaseImage() {
    if (!m_image.isNull()) {
        m_device.destroy(m_image);
        m_image = {};
    }
}

// ---------------------------------------------------------------------------------------------------------
// Device

NullDevice::NullDevice(const DeviceDesc& desc, platform::Window* window)
    : m_validate(desc.enableValidation) {
    if (window != nullptr) {
        m_swapchain = std::make_unique<NullSwapchain>(*this, desc.swapchain, window->framebufferSize());
    }
}

NullDevice::~NullDevice() {
    m_swapchain.reset(); // its image is a texture of this device
}

template <typename Desc>
NullDevice::Object<Desc> NullDevice::store(const Desc& desc) {
    auto object = std::make_unique<Stored<Desc>>();
    object->desc = desc;
    object->debugName = desc.debugName;
    object->desc.debugName = object->debugName;
    if constexpr (std::is_same_v<Desc, ShaderModuleDesc>) {
        object->blob.assign(desc.blob.begin(), desc.blob.end());
        object->entryPoint = desc.entryPoint;
        object->desc.blob = object->blob;
        object->desc.entryPoint = object->entryPoint;
    } else if constexpr (std::is_same_v<Desc, GraphicsPipelineDesc>) {
        object->vertexBuffers.assign(desc.vertexBuffers.begin(), desc.vertexBuffers.end());
        object->vertexAttributes.assign(desc.vertexAttributes.begin(), desc.vertexAttributes.end());
        object->desc.vertexBuffers = object->vertexBuffers;
        object->desc.vertexAttributes = object->vertexAttributes;
    }
    return object;
}

Result<BufferHandle> NullDevice::createBuffer(const BufferDesc& desc) {
    if (m_validate) {
        validateBufferDesc(desc);
    }
    return m_buffers.create(store(desc));
}

Result<TextureHandle> NullDevice::createTexture(const TextureDesc& desc) {
    if (m_validate) {
        validateTextureDesc(desc, m_limits);
    }
    return m_textures.create(store(desc));
}

Result<SamplerHandle> NullDevice::createSampler(const SamplerDesc& desc) {
    if (m_validate) {
        validateSamplerDesc(desc);
    }
    return m_samplers.create(store(desc));
}

Result<ShaderModuleHandle> NullDevice::createShaderModule(const ShaderModuleDesc& desc) {
    if (m_validate) {
        validateShaderModuleDesc(desc);
    }
    return m_shaderModules.create(store(desc));
}

Result<GraphicsPipelineHandle> NullDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    if (m_validate) {
        validateGraphicsPipelineDesc(desc);
        TESERRA_ASSERT(m_shaderModules.contains(desc.vertexShader),
                       "Graphics pipeline '{}': vertex shader {} is not alive", desc.debugName,
                       desc.vertexShader);
        TESERRA_ASSERT(desc.fragmentShader.isNull() || m_shaderModules.contains(desc.fragmentShader),
                       "Graphics pipeline '{}': fragment shader {} is not alive", desc.debugName,
                       desc.fragmentShader);
    }
    return m_pipelines.create(store(desc));
}

TextureHandle NullDevice::createSwapchainImage(Format format, UVec2 size) {
    return m_textures.create(store(TextureDesc{
        .format = format,
        .width = size.x,
        .height = size.y,
        .usage = TextureUsage::RenderTarget | TextureUsage::CopySrc,
        .debugName = "Swapchain image",
    }));
}

void NullDevice::destroy(BufferHandle handle) {
    if (!handle.isNull()) {
        m_buffers.destroy(handle);
    }
}

void NullDevice::destroy(TextureHandle handle) {
    if (!handle.isNull()) {
        m_textures.destroy(handle);
    }
}

void NullDevice::destroy(SamplerHandle handle) {
    if (!handle.isNull()) {
        m_samplers.destroy(handle);
    }
}

void NullDevice::destroy(ShaderModuleHandle handle) {
    if (!handle.isNull()) {
        m_shaderModules.destroy(handle);
    }
}

void NullDevice::destroy(GraphicsPipelineHandle handle) {
    if (!handle.isNull()) {
        m_pipelines.destroy(handle);
    }
}

void NullDevice::beginFrame() {
    TESERRA_ASSERT(!m_inFrame, "beginFrame() twice without endFrame()");
    for (CommandList& list : m_commandLists[m_frame % kMaxFramesInFlight]) {
        list.reset();
    }
    m_commandListsInUse = 0;
    m_submitted.clear();
    m_inFrame = true;
}

void NullDevice::endFrame() {
    TESERRA_ASSERT(m_inFrame, "endFrame() without beginFrame()");
    m_inFrame = false;
    ++m_frame;
}

CommandList& NullDevice::acquireCommandList() {
    TESERRA_ASSERT(m_inFrame, "acquireCommandList() outside beginFrame() / endFrame()");
    std::deque<CommandList>& lists = m_commandLists[m_frame % kMaxFramesInFlight];
    if (m_commandListsInUse == lists.size()) {
        lists.emplace_back();
    }
    return lists[m_commandListsInUse++];
}

void NullDevice::submit(CommandList& commands) {
    commands.finish();
    if (m_validate) {
        validate(commands);
    }
    m_submitted.push_back(&commands);
}

usize NullDevice::liveObjectCount() const noexcept {
    usize count = 0;
    const auto add = [&](auto /*handle*/, const auto& /*object*/) { ++count; };
    m_buffers.forEach(add);
    m_textures.forEach(add);
    m_samplers.forEach(add);
    m_shaderModules.forEach(add);
    m_pipelines.forEach(add);
    return count;
}

const BufferDesc* NullDevice::buffer(BufferHandle handle) const noexcept {
    return find(m_buffers, handle);
}

const TextureDesc* NullDevice::texture(TextureHandle handle) const noexcept {
    return find(m_textures, handle);
}

const SamplerDesc* NullDevice::sampler(SamplerHandle handle) const noexcept {
    return find(m_samplers, handle);
}

const ShaderModuleDesc* NullDevice::shaderModule(ShaderModuleHandle handle) const noexcept {
    return find(m_shaderModules, handle);
}

const GraphicsPipelineDesc* NullDevice::graphicsPipeline(GraphicsPipelineHandle handle) const noexcept {
    return find(m_pipelines, handle);
}

// ---------------------------------------------------------------------------------------------------------
// Submit-time validation (docs/architecture/rhi.md#validation-layer)

void NullDevice::validate(const CommandList& commands) const {
    // Formats of the open render pass's attachments, which a bound pipeline must declare.
    std::array<Format, kMaxColorAttachments> passColorFormats{};
    Format passDepthFormat = Format::Undefined;

    const auto liveBuffer = [&](BufferHandle handle, [[maybe_unused]] std::string_view use,
                                BufferUsage usage) {
        const BufferDesc* desc = buffer(handle);
        TESERRA_ASSERT(desc != nullptr, "{} uses buffer {}, which is not alive", use, handle);
        TESERRA_ASSERT(desc == nullptr || hasAll(desc->usage, usage),
                       "{} uses buffer '{}', created without {}", use, desc->debugName, usageName(usage));
        return desc;
    };
    const auto liveTexture = [&](TextureHandle handle, [[maybe_unused]] std::string_view use,
                                 TextureUsage usage) {
        const TextureDesc* desc = texture(handle);
        TESERRA_ASSERT(desc != nullptr, "{} uses texture {}, which is not alive", use, handle);
        TESERRA_ASSERT(desc == nullptr || hasAll(desc->usage, usage),
                       "{} uses texture '{}', created without {}", use, desc->debugName, usageName(usage));
        return desc;
    };
    const auto checkTextureCopy = [&](const BufferTextureCopy& copy, [[maybe_unused]] std::string_view use,
                                      bool toTexture) {
        const BufferDesc* bufferDesc =
            liveBuffer(copy.buffer, use, toTexture ? BufferUsage::CopySrc : BufferUsage::CopyDst);
        const TextureDesc* textureDesc =
            liveTexture(copy.texture, use, toTexture ? TextureUsage::CopyDst : TextureUsage::CopySrc);
        if (bufferDesc == nullptr || textureDesc == nullptr) {
            return;
        }
        const u32 width = std::max(textureDesc->width >> copy.mipLevel, 1U);
        const u32 height = std::max(textureDesc->height >> copy.mipLevel, 1U);
        TESERRA_ASSERT(
            copy.x + copy.width <= width && copy.y + copy.height <= height &&
                copy.arrayLayer + copy.layerCount <= textureDesc->depthOrLayers,
            "{} region ({}, {}) {}x{}, layers {}+{} is outside texture '{}' ({}x{} at mip {}, {} layers)",
            use, copy.x, copy.y, copy.width, copy.height, copy.arrayLayer, copy.layerCount,
            textureDesc->debugName, width, height, copy.mipLevel, textureDesc->depthOrLayers);
        TESERRA_ASSERT(copy.bufferOffset + bufferBytes(copy, textureDesc->format) <= bufferDesc->sizeInBytes,
                       "{} needs {} bytes from offset {} of buffer '{}', which has {}", use,
                       bufferBytes(copy, textureDesc->format), copy.bufferOffset, bufferDesc->debugName,
                       bufferDesc->sizeInBytes);
    };

    for (const Command command : commands.commands()) {
        switch (command.type()) {
            case CommandType::BeginRenderPass: {
                const RenderPassDesc& pass = command.as<BeginRenderPassCommand>().pass;
                passColorFormats.fill(Format::Undefined);
                passDepthFormat = Format::Undefined;
                for (u32 i = 0; i < kMaxColorAttachments && !pass.colorAttachments[i].texture.isNull(); ++i) {
                    if (const TextureDesc* desc =
                            liveTexture(pass.colorAttachments[i].texture, "A color attachment",
                                        TextureUsage::RenderTarget)) {
                        passColorFormats[i] = desc->format;
                    }
                }
                if (!pass.depthStencil.texture.isNull()) {
                    if (const TextureDesc* desc =
                            liveTexture(pass.depthStencil.texture, "The depth-stencil attachment",
                                        TextureUsage::DepthStencil)) {
                        passDepthFormat = desc->format;
                    }
                }
                break;
            }
            case CommandType::BindPipeline: {
                const GraphicsPipelineHandle handle = command.as<BindPipelineCommand>().pipeline;
                const GraphicsPipelineDesc* desc = graphicsPipeline(handle);
                TESERRA_ASSERT(desc != nullptr, "bindPipeline uses pipeline {}, which is not alive", handle);
                if (desc == nullptr) {
                    break;
                }
                for (u32 i = 0; i < kMaxColorAttachments; ++i) {
                    TESERRA_ASSERT(
                        desc->targets.colorFormats[i] == passColorFormats[i],
                        "Pipeline '{}' declares {} for color target {}, but the render pass has {}",
                        desc->debugName, formatInfo(desc->targets.colorFormats[i]).name, i,
                        formatInfo(passColorFormats[i]).name);
                }
                TESERRA_ASSERT(
                    desc->targets.depthStencilFormat == passDepthFormat,
                    "Pipeline '{}' declares the depth-stencil format {}, but the render pass has {}",
                    desc->debugName, formatInfo(desc->targets.depthStencilFormat).name,
                    formatInfo(passDepthFormat).name);
                break;
            }
            case CommandType::BindVertexBuffers:
                for (const VertexBufferBinding& binding : command.vertexBuffers()) {
                    if (const BufferDesc* desc =
                            liveBuffer(binding.buffer, "bindVertexBuffers", BufferUsage::Vertex)) {
                        TESERRA_ASSERT(
                            binding.offset < desc->sizeInBytes,
                            "bindVertexBuffers offset {} is past the end of buffer '{}' ({} bytes)",
                            binding.offset, desc->debugName, desc->sizeInBytes);
                    }
                }
                break;
            case CommandType::BindIndexBuffer: {
                const auto& bind = command.as<BindIndexBufferCommand>();
                if (const BufferDesc* desc = liveBuffer(bind.buffer, "bindIndexBuffer", BufferUsage::Index)) {
                    TESERRA_ASSERT(bind.offset < desc->sizeInBytes,
                                   "bindIndexBuffer offset {} is past the end of buffer '{}' ({} bytes)",
                                   bind.offset, desc->debugName, desc->sizeInBytes);
                }
                break;
            }
            case CommandType::CopyBuffer: {
                const BufferCopy& copy = command.as<CopyBufferCommand>().copy;
                const BufferDesc* source = liveBuffer(copy.source, "copyBuffer", BufferUsage::CopySrc);
                const BufferDesc* destination =
                    liveBuffer(copy.destination, "copyBuffer", BufferUsage::CopyDst);
                TESERRA_ASSERT(source == nullptr || copy.sourceOffset + copy.size <= source->sizeInBytes,
                               "copyBuffer reads {} bytes from offset {} of buffer '{}', which has {}",
                               copy.size, copy.sourceOffset, source->debugName, source->sizeInBytes);
                TESERRA_ASSERT(
                    destination == nullptr || copy.destinationOffset + copy.size <= destination->sizeInBytes,
                    "copyBuffer writes {} bytes at offset {} of buffer '{}', which has {}", copy.size,
                    copy.destinationOffset, destination->debugName, destination->sizeInBytes);
                break;
            }
            case CommandType::CopyBufferToTexture:
                checkTextureCopy(command.as<CopyBufferToTextureCommand>().copy, "copyBufferToTexture", true);
                break;
            case CommandType::CopyTextureToBuffer:
                checkTextureCopy(command.as<CopyTextureToBufferCommand>().copy, "copyTextureToBuffer", false);
                break;
            default:
                // TODO(#42): bind group liveness and compatibility with the pipeline layout
                break;
        }
    }
}

} // namespace teserra::rhi::null
