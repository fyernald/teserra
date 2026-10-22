// Submit-time validation of command streams against a device's objects
// (docs/architecture/rhi.md#validation-layer).

#include <teserra/core/assert.hpp>
#include <teserra/rhi/commands.hpp>
#include <teserra/rhi/format.hpp>
#include <teserra/rhi/validation.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

namespace teserra::rhi {

namespace {

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

void validateCommands(const CommandList& commands, const ResourceLookup& objects) {
    // Formats of the open render pass's attachments, which a bound pipeline must declare.
    std::array<Format, kMaxColorAttachments> passColorFormats{};
    Format passDepthFormat = Format::Undefined;

    const auto liveBuffer = [&](BufferHandle handle, [[maybe_unused]] std::string_view use,
                                BufferUsage usage) {
        std::optional<BufferDesc> desc = objects.buffer(handle);
        TESERRA_ASSERT(desc.has_value(), "{} uses buffer {}, which is not alive", use, handle);
        TESERRA_ASSERT(!desc || hasAll(desc->usage, usage), "{} uses buffer '{}', created without {}", use,
                       desc->debugName, usageName(usage));
        return desc;
    };
    const auto liveTexture = [&](TextureHandle handle, [[maybe_unused]] std::string_view use,
                                 TextureUsage usage) {
        std::optional<TextureDesc> desc = objects.texture(handle);
        TESERRA_ASSERT(desc.has_value(), "{} uses texture {}, which is not alive", use, handle);
        TESERRA_ASSERT(!desc || hasAll(desc->usage, usage), "{} uses texture '{}', created without {}", use,
                       desc->debugName, usageName(usage));
        return desc;
    };
    const auto checkTextureCopy = [&](const BufferTextureCopy& copy, [[maybe_unused]] std::string_view use,
                                      bool toTexture) {
        const std::optional<BufferDesc> bufferDesc =
            liveBuffer(copy.buffer, use, toTexture ? BufferUsage::CopySrc : BufferUsage::CopyDst);
        const std::optional<TextureDesc> textureDesc =
            liveTexture(copy.texture, use, toTexture ? TextureUsage::CopyDst : TextureUsage::CopySrc);
        if (!bufferDesc || !textureDesc) {
            return;
        }
        [[maybe_unused]] const u32 width = std::max(textureDesc->width >> copy.mipLevel, 1U);
        [[maybe_unused]] const u32 height = std::max(textureDesc->height >> copy.mipLevel, 1U);
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
                    if (const std::optional<TextureDesc> desc =
                            liveTexture(pass.colorAttachments[i].texture, "A color attachment",
                                        TextureUsage::RenderTarget)) {
                        passColorFormats[i] = desc->format;
                    }
                }
                if (!pass.depthStencil.texture.isNull()) {
                    if (const std::optional<TextureDesc> desc =
                            liveTexture(pass.depthStencil.texture, "The depth-stencil attachment",
                                        TextureUsage::DepthStencil)) {
                        passDepthFormat = desc->format;
                    }
                }
                break;
            }
            case CommandType::BindPipeline: {
                const GraphicsPipelineHandle handle = command.as<BindPipelineCommand>().pipeline;
                const std::optional<GraphicsPipelineDesc> desc = objects.graphicsPipeline(handle);
                TESERRA_ASSERT(desc.has_value(), "bindPipeline uses pipeline {}, which is not alive", handle);
                if (!desc) {
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
                    if (const std::optional<BufferDesc> desc =
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
                if (const std::optional<BufferDesc> desc =
                        liveBuffer(bind.buffer, "bindIndexBuffer", BufferUsage::Index)) {
                    TESERRA_ASSERT(bind.offset < desc->sizeInBytes,
                                   "bindIndexBuffer offset {} is past the end of buffer '{}' ({} bytes)",
                                   bind.offset, desc->debugName, desc->sizeInBytes);
                }
                break;
            }
            case CommandType::CopyBuffer: {
                const BufferCopy& copy = command.as<CopyBufferCommand>().copy;
                const std::optional<BufferDesc> source =
                    liveBuffer(copy.source, "copyBuffer", BufferUsage::CopySrc);
                const std::optional<BufferDesc> destination =
                    liveBuffer(copy.destination, "copyBuffer", BufferUsage::CopyDst);
                TESERRA_ASSERT(!source || copy.sourceOffset + copy.size <= source->sizeInBytes,
                               "copyBuffer reads {} bytes from offset {} of buffer '{}', which has {}",
                               copy.size, copy.sourceOffset, source->debugName, source->sizeInBytes);
                TESERRA_ASSERT(!destination || copy.destinationOffset + copy.size <= destination->sizeInBytes,
                               "copyBuffer writes {} bytes at offset {} of buffer '{}', which has {}",
                               copy.size, copy.destinationOffset, destination->debugName,
                               destination->sizeInBytes);
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

} // namespace teserra::rhi
