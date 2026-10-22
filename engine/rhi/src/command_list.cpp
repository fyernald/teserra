#include <teserra/rhi/command_list.hpp>

#include <teserra/core/assert.hpp>
#include <teserra/rhi/limits.hpp>

#include <algorithm>
#include <cstring>
#include <format>
#include <iterator>
#include <limits>

namespace teserra::rhi {

namespace {

static_assert(static_cast<usize>(CommandType::PopDebugGroup) + 1 == kCommandTypeCount,
              "kCommandTypeCount must count every CommandType");
// The stream's storage comes from operator new, which aligns at least this much.
static_assert(__STDCPP_DEFAULT_NEW_ALIGNMENT__ >= kCommandAlignment);

// Capacity of a stream on its first record.
constexpr usize kInitialCapacity = 4096;

constexpr usize alignRecord(usize size) noexcept {
    return (size + kCommandAlignment - 1) / kCommandAlignment * kCommandAlignment;
}

std::string_view loadOpName(LoadOp op) noexcept {
    switch (op) {
        case LoadOp::Load:
            return "Load";
        case LoadOp::Clear:
            return "Clear";
        case LoadOp::DontCare:
            return "DontCare";
    }
    return "Unknown";
}

std::string_view storeOpName(StoreOp op) noexcept {
    switch (op) {
        case StoreOp::Store:
            return "Store";
        case StoreOp::DontCare:
            return "DontCare";
    }
    return "Unknown";
}

std::string_view indexTypeName(IndexType type) noexcept {
    switch (type) {
        case IndexType::Uint16:
            return "Uint16";
        case IndexType::Uint32:
            return "Uint32";
    }
    return "Unknown";
}

// Number of color attachments in use: the leading ones with a texture.
u32 colorAttachmentCount(const RenderPassDesc& desc) noexcept {
    u32 count = 0;
    while (count < kMaxColorAttachments && !desc.colorAttachments[count].texture.isNull()) {
        ++count;
    }
    return count;
}

using Output = std::back_insert_iterator<std::string>;

void describeRenderPass(Output out, const RenderPassDesc& pass) {
    const u32 colorCount = colorAttachmentCount(pass);
    for (u32 i = 0; i < colorCount; ++i) {
        const ColorAttachment& color = pass.colorAttachments[i];
        std::format_to(out, " color{}={}", i, color.texture);
        if (color.mipLevel != 0 || color.arrayLayer != 0) {
            std::format_to(out, " mip={} layer={}", color.mipLevel, color.arrayLayer);
        }
        std::format_to(out, " load={} store={}", loadOpName(color.loadOp), storeOpName(color.storeOp));
        if (color.loadOp == LoadOp::Clear) {
            std::format_to(out, " clear=({}, {}, {}, {})", color.clearColor.x, color.clearColor.y,
                           color.clearColor.z, color.clearColor.w);
        }
    }
    const DepthStencilAttachment& depth = pass.depthStencil;
    if (depth.texture.isNull()) {
        return;
    }
    std::format_to(out, " depth={}", depth.texture);
    if (depth.mipLevel != 0 || depth.arrayLayer != 0) {
        std::format_to(out, " mip={} layer={}", depth.mipLevel, depth.arrayLayer);
    }
    std::format_to(out, " load={} store={}", loadOpName(depth.depthLoadOp), storeOpName(depth.depthStoreOp));
    if (depth.depthLoadOp == LoadOp::Clear) {
        std::format_to(out, " clear={}", depth.clearDepth);
    }
    if (depth.stencilLoadOp != LoadOp::DontCare || depth.stencilStoreOp != StoreOp::DontCare) {
        std::format_to(out, " stencilLoad={} stencilStore={}", loadOpName(depth.stencilLoadOp),
                       storeOpName(depth.stencilStoreOp));
        if (depth.stencilLoadOp == LoadOp::Clear) {
            std::format_to(out, " clearStencil={}", depth.clearStencil);
        }
    }
}

void describeTextureCopy(Output out, const BufferTextureCopy& copy) {
    std::format_to(out, " buffer={}+{} bytesPerRow={} texture={} mip={} layers={}+{} region=({}, {}) {}x{}",
                   copy.buffer, copy.bufferOffset, copy.bytesPerRow, copy.texture, copy.mipLevel,
                   copy.arrayLayer, copy.layerCount, copy.x, copy.y, copy.width, copy.height);
}

void describeCommand(Output out, const Command command) {
    std::format_to(out, "{}", commandTypeName(command.type()));
    switch (command.type()) {
        case CommandType::BeginRenderPass:
            describeRenderPass(out, command.as<BeginRenderPassCommand>().pass);
            break;
        case CommandType::EndRenderPass:
        case CommandType::PopDebugGroup:
            break;
        case CommandType::BindPipeline:
            std::format_to(out, " pipeline={}", command.as<BindPipelineCommand>().pipeline);
            break;
        case CommandType::BindGroup: {
            const auto& bind = command.as<BindGroupCommand>();
            std::format_to(out, " index={} group={}", bind.index, bind.group);
            if (bind.dynamicOffsetCount > 0) {
                std::format_to(out, " dynamicOffsets=[");
                const char* separator = "";
                for (const u32 offset : command.dynamicOffsets()) {
                    std::format_to(out, "{}{}", separator, offset);
                    separator = ", ";
                }
                std::format_to(out, "]");
            }
            break;
        }
        case CommandType::BindVertexBuffers: {
            std::format_to(out, " firstSlot={} buffers=[", command.as<BindVertexBuffersCommand>().firstSlot);
            const char* separator = "";
            for (const VertexBufferBinding& binding : command.vertexBuffers()) {
                std::format_to(out, "{}{}+{}", separator, binding.buffer, binding.offset);
                separator = ", ";
            }
            std::format_to(out, "]");
            break;
        }
        case CommandType::BindIndexBuffer: {
            const auto& bind = command.as<BindIndexBufferCommand>();
            std::format_to(out, " buffer={}+{} indexType={}", bind.buffer, bind.offset,
                           indexTypeName(bind.indexType));
            break;
        }
        case CommandType::SetViewport: {
            const Viewport& viewport = command.as<SetViewportCommand>().viewport;
            std::format_to(out, " x={} y={} width={} height={} minDepth={} maxDepth={}", viewport.x,
                           viewport.y, viewport.width, viewport.height, viewport.minDepth, viewport.maxDepth);
            break;
        }
        case CommandType::SetScissor: {
            const ScissorRect& scissor = command.as<SetScissorCommand>().scissor;
            std::format_to(out, " x={} y={} width={} height={}", scissor.x, scissor.y, scissor.width,
                           scissor.height);
            break;
        }
        case CommandType::Draw: {
            const DrawArgs& args = command.as<DrawCommand>().args;
            std::format_to(out, " vertexCount={} instanceCount={} firstVertex={} firstInstance={}",
                           args.vertexCount, args.instanceCount, args.firstVertex, args.firstInstance);
            break;
        }
        case CommandType::DrawIndexed: {
            const DrawIndexedArgs& args = command.as<DrawIndexedCommand>().args;
            std::format_to(
                out, " indexCount={} instanceCount={} firstIndex={} vertexOffset={} firstInstance={}",
                args.indexCount, args.instanceCount, args.firstIndex, args.vertexOffset, args.firstInstance);
            break;
        }
        case CommandType::CopyBuffer: {
            const BufferCopy& copy = command.as<CopyBufferCommand>().copy;
            std::format_to(out, " source={}+{} destination={}+{} size={}", copy.source, copy.sourceOffset,
                           copy.destination, copy.destinationOffset, copy.size);
            break;
        }
        case CommandType::CopyBufferToTexture:
            describeTextureCopy(out, command.as<CopyBufferToTextureCommand>().copy);
            break;
        case CommandType::CopyTextureToBuffer:
            describeTextureCopy(out, command.as<CopyTextureToBufferCommand>().copy);
            break;
        case CommandType::PushDebugGroup:
            std::format_to(out, " \"{}\"", command.debugLabel());
            break;
    }
}

} // namespace

std::string_view commandTypeName(CommandType type) noexcept {
    switch (type) {
        case CommandType::BeginRenderPass:
            return "BeginRenderPass";
        case CommandType::EndRenderPass:
            return "EndRenderPass";
        case CommandType::BindPipeline:
            return "BindPipeline";
        case CommandType::BindGroup:
            return "BindGroup";
        case CommandType::BindVertexBuffers:
            return "BindVertexBuffers";
        case CommandType::BindIndexBuffer:
            return "BindIndexBuffer";
        case CommandType::SetViewport:
            return "SetViewport";
        case CommandType::SetScissor:
            return "SetScissor";
        case CommandType::Draw:
            return "Draw";
        case CommandType::DrawIndexed:
            return "DrawIndexed";
        case CommandType::CopyBuffer:
            return "CopyBuffer";
        case CommandType::CopyBufferToTexture:
            return "CopyBufferToTexture";
        case CommandType::CopyTextureToBuffer:
            return "CopyTextureToBuffer";
        case CommandType::PushDebugGroup:
            return "PushDebugGroup";
        case CommandType::PopDebugGroup:
            return "PopDebugGroup";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------------------------------------
// Render passes

void CommandList::beginRenderPass(const RenderPassDesc& desc) {
    requireRecording();
    TESERRA_ASSERT(!m_inRenderPass, "beginRenderPass inside a render pass: render passes do not nest");
    const u32 colorCount = colorAttachmentCount(desc);
    for (u32 i = colorCount; i < kMaxColorAttachments; ++i) {
        TESERRA_ASSERT(desc.colorAttachments[i].texture.isNull(),
                       "Color attachment {} follows the unused attachment {}: attachments in use must be the "
                       "leading ones",
                       i, colorCount);
    }
    TESERRA_ASSERT(colorCount > 0 || !desc.depthStencil.texture.isNull(),
                   "beginRenderPass without attachments");

    append(BeginRenderPassCommand{desc});
    m_inRenderPass = true;
    m_pipelineBound = false;
    m_indexBufferBound = false;
    m_renderPassDebugGroupDepth = m_debugGroupDepth;
}

void CommandList::endRenderPass() {
    requireRecording();
    TESERRA_ASSERT(m_inRenderPass, "endRenderPass outside a render pass");
    TESERRA_ASSERT(m_debugGroupDepth == m_renderPassDebugGroupDepth,
                   "endRenderPass with {} debug group(s) of the render pass still open",
                   m_debugGroupDepth - m_renderPassDebugGroupDepth);

    append(CommandType::EndRenderPass, nullptr, 0, 0);
    m_inRenderPass = false;
    m_pipelineBound = false;
    m_indexBufferBound = false;
}

// ---------------------------------------------------------------------------------------------------------
// Bindings

void CommandList::bindPipeline(GraphicsPipelineHandle pipeline) {
    requireInsideRenderPass("bindPipeline");
    TESERRA_ASSERT(!pipeline.isNull(), "bindPipeline with a null pipeline");

    append(BindPipelineCommand{pipeline});
    m_pipelineBound = true;
}

void CommandList::bindGroup(u32 index, BindGroupHandle group, std::span<const u32> dynamicOffsets) {
    requireInsideRenderPass("bindGroup");
    TESERRA_ASSERT(index < kMaxBindGroups, "bindGroup at index {}; the limit is {} bind groups", index,
                   kMaxBindGroups);
    TESERRA_ASSERT(!group.isNull(), "bindGroup with a null bind group at index {}", index);
    TESERRA_ASSERT(dynamicOffsets.size() <= kMaxDynamicOffsets,
                   "bindGroup with {} dynamic offsets; the limit is {}", dynamicOffsets.size(),
                   kMaxDynamicOffsets);

    const BindGroupCommand command{
        .index = index,
        .group = group,
        .dynamicOffsetCount = static_cast<u32>(dynamicOffsets.size()),
    };
    std::byte* trailing = append(command, dynamicOffsets.size_bytes());
    if (!dynamicOffsets.empty()) {
        std::memcpy(trailing, dynamicOffsets.data(), dynamicOffsets.size_bytes());
    }
}

void CommandList::bindVertexBuffers(u32 firstSlot, std::span<const VertexBufferBinding> bindings) {
    requireInsideRenderPass("bindVertexBuffers");
    TESERRA_ASSERT(!bindings.empty(), "bindVertexBuffers without bindings");
    TESERRA_ASSERT(firstSlot <= kMaxVertexBuffers && bindings.size() <= kMaxVertexBuffers - firstSlot,
                   "bindVertexBuffers to slots {} to {}; the limit is {} vertex buffers", firstSlot,
                   firstSlot + bindings.size() - 1, kMaxVertexBuffers);
    for (usize i = 0; i < bindings.size(); ++i) {
        TESERRA_ASSERT(!bindings[i].buffer.isNull(), "bindVertexBuffers with a null buffer for slot {}",
                       firstSlot + i);
    }

    const BindVertexBuffersCommand command{.firstSlot = firstSlot,
                                           .count = static_cast<u32>(bindings.size())};
    std::byte* trailing = append(command, bindings.size_bytes());
    std::memcpy(trailing, bindings.data(), bindings.size_bytes());
}

void CommandList::bindVertexBuffer(u32 slot, BufferHandle buffer, u64 offset) {
    const VertexBufferBinding binding{.buffer = buffer, .offset = offset};
    bindVertexBuffers(slot, {&binding, 1});
}

void CommandList::bindIndexBuffer(BufferHandle buffer, IndexType indexType, u64 offset) {
    requireInsideRenderPass("bindIndexBuffer");
    TESERRA_ASSERT(!buffer.isNull(), "bindIndexBuffer with a null buffer");
    TESERRA_ASSERT(offset % indexSize(indexType) == 0,
                   "bindIndexBuffer offset {} is not a multiple of the index size {}", offset,
                   indexSize(indexType));

    append(BindIndexBufferCommand{.buffer = buffer, .indexType = indexType, .offset = offset});
    m_indexBufferBound = true;
}

void CommandList::setViewport(const Viewport& viewport) {
    requireInsideRenderPass("setViewport");
    TESERRA_ASSERT(viewport.width > 0.0F && viewport.height > 0.0F,
                   "setViewport with an empty viewport ({}x{})", viewport.width, viewport.height);
    TESERRA_ASSERT(viewport.minDepth >= 0.0F && viewport.minDepth <= 1.0F && viewport.maxDepth >= 0.0F &&
                       viewport.maxDepth <= 1.0F,
                   "setViewport depth range [{}, {}] is outside [0, 1]", viewport.minDepth,
                   viewport.maxDepth);

    append(SetViewportCommand{viewport});
}

void CommandList::setScissor(const ScissorRect& scissor) {
    requireInsideRenderPass("setScissor");
    TESERRA_ASSERT(scissor.width > 0 && scissor.height > 0, "setScissor with an empty rectangle ({}x{})",
                   scissor.width, scissor.height);

    append(SetScissorCommand{scissor});
}

// ---------------------------------------------------------------------------------------------------------
// Draws

void CommandList::draw(const DrawArgs& args) {
    requireInsideRenderPass("draw");
    TESERRA_ASSERT(m_pipelineBound, "draw without a pipeline bound in this render pass");

    append(DrawCommand{args});
}

void CommandList::drawIndexed(const DrawIndexedArgs& args) {
    requireInsideRenderPass("drawIndexed");
    TESERRA_ASSERT(m_pipelineBound, "drawIndexed without a pipeline bound in this render pass");
    TESERRA_ASSERT(m_indexBufferBound, "drawIndexed without an index buffer bound in this render pass");

    append(DrawIndexedCommand{args});
}

// ---------------------------------------------------------------------------------------------------------
// Copies

void CommandList::copyBuffer(const BufferCopy& copy) {
    requireOutsideRenderPass("copyBuffer");
    TESERRA_ASSERT(!copy.source.isNull() && !copy.destination.isNull(), "copyBuffer with a null buffer");
    TESERRA_ASSERT(copy.size > 0, "copyBuffer of 0 bytes");
    TESERRA_ASSERT(copy.source != copy.destination ||
                       copy.sourceOffset + copy.size <= copy.destinationOffset ||
                       copy.destinationOffset + copy.size <= copy.sourceOffset,
                   "copyBuffer between overlapping ranges of buffer {}", copy.source);

    append(CopyBufferCommand{copy});
}

void CommandList::copyBufferToTexture(const BufferTextureCopy& copy) {
    requireOutsideRenderPass("copyBufferToTexture");
    TESERRA_ASSERT(!copy.buffer.isNull() && !copy.texture.isNull(),
                   "copyBufferToTexture with a null resource");
    TESERRA_ASSERT(copy.width > 0 && copy.height > 0 && copy.layerCount > 0,
                   "copyBufferToTexture of an empty region ({}x{}, {} layers)", copy.width, copy.height,
                   copy.layerCount);

    append(CopyBufferToTextureCommand{copy});
}

void CommandList::copyTextureToBuffer(const BufferTextureCopy& copy) {
    requireOutsideRenderPass("copyTextureToBuffer");
    TESERRA_ASSERT(!copy.buffer.isNull() && !copy.texture.isNull(),
                   "copyTextureToBuffer with a null resource");
    TESERRA_ASSERT(copy.width > 0 && copy.height > 0 && copy.layerCount > 0,
                   "copyTextureToBuffer of an empty region ({}x{}, {} layers)", copy.width, copy.height,
                   copy.layerCount);

    append(CopyTextureToBufferCommand{copy});
}

// ---------------------------------------------------------------------------------------------------------
// Debug groups

void CommandList::pushDebugGroup(std::string_view label) {
    requireRecording();
    TESERRA_ASSERT(label.size() <= kMaxDebugLabelLength, "pushDebugGroup label of {} bytes; the limit is {}",
                   label.size(), kMaxDebugLabelLength);

    std::byte* trailing = append(PushDebugGroupCommand{static_cast<u32>(label.size())}, label.size());
    if (!label.empty()) {
        std::memcpy(trailing, label.data(), label.size());
    }
    ++m_debugGroupDepth;
}

void CommandList::popDebugGroup() {
    requireRecording();
    TESERRA_ASSERT(m_debugGroupDepth > 0, "popDebugGroup without a matching pushDebugGroup");
    TESERRA_ASSERT(!m_inRenderPass || m_debugGroupDepth > m_renderPassDebugGroupDepth,
                   "popDebugGroup inside a render pass closes a debug group opened before the pass");

    append(CommandType::PopDebugGroup, nullptr, 0, 0);
    --m_debugGroupDepth;
}

// ---------------------------------------------------------------------------------------------------------
// Lifecycle

void CommandList::finish() {
    requireRecording();
    TESERRA_ASSERT(!m_inRenderPass, "Finishing a command list with an open render pass");
    TESERRA_ASSERT(m_debugGroupDepth == 0, "Finishing a command list with {} open debug group(s)",
                   m_debugGroupDepth);
    m_finished = true;
}

void CommandList::reset() noexcept {
    m_size = 0;
    m_commandCount = 0;
    m_debugGroupDepth = 0;
    m_renderPassDebugGroupDepth = 0;
    m_inRenderPass = false;
    m_pipelineBound = false;
    m_indexBufferBound = false;
    m_finished = false;
}

std::byte* CommandList::append(CommandType type, const void* payload, usize payloadSize, usize trailingSize) {
    const usize trailingOffset = commandTrailingOffset(payloadSize);
    const usize recordSize = alignRecord(trailingOffset + trailingSize);
    TESERRA_ASSERT(recordSize <= std::numeric_limits<u32>::max(), "A {} command of {} bytes",
                   commandTypeName(type), recordSize);

    if (m_size + recordSize > m_bytes.size()) {
        // Grows while the stream warms up to a frame's size; reset() keeps the capacity.
        m_bytes.resize(std::max({m_bytes.size() * 2, m_size + recordSize, kInitialCapacity}));
    }
    std::byte* record = m_bytes.data() + m_size;
    const CommandHeader header{.type = type, .size = static_cast<u32>(recordSize)};
    std::memcpy(record, &header, sizeof(header));
    if (payloadSize > 0) {
        std::memcpy(record + sizeof(CommandHeader), payload, payloadSize);
    }
    m_size += recordSize;
    ++m_commandCount;
    return record + trailingOffset;
}

void CommandList::requireRecording() const {
    TESERRA_ASSERT(!m_finished, "Recording into a finished command list; reset() it first");
}

// `command` is only used by the checks, which Release builds compile out.
void CommandList::requireInsideRenderPass([[maybe_unused]] std::string_view command) const {
    requireRecording();
    TESERRA_ASSERT(m_inRenderPass, "{} outside a render pass", command);
}

void CommandList::requireOutsideRenderPass([[maybe_unused]] std::string_view command) const {
    requireRecording();
    TESERRA_ASSERT(!m_inRenderPass, "{} inside a render pass: copies happen outside render passes", command);
}

// ---------------------------------------------------------------------------------------------------------
// Debugging

std::string describeCommands(const CommandList& list) {
    std::string text;
    auto out = std::back_inserter(text);
    usize depth = 0;
    for (const Command command : list.commands()) {
        if ((command.type() == CommandType::EndRenderPass || command.type() == CommandType::PopDebugGroup) &&
            depth > 0) {
            --depth;
        }
        std::format_to(out, "{:{}}", "", depth * 2);
        describeCommand(out, command);
        text += '\n';
        if (command.type() == CommandType::BeginRenderPass || command.type() == CommandType::PushDebugGroup) {
            ++depth;
        }
    }
    return text;
}

} // namespace teserra::rhi
