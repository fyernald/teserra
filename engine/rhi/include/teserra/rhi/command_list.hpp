#pragma once

// Command recording (docs/architecture/rhi.md#command-recording-and-submission). A CommandList appends
// commands as POD records to a byte stream that it reuses from frame to frame; recording makes no backend
// calls and, once the stream has grown to a frame's size, no heap allocations. At submit, the backend
// replays (OpenGL) or translates (Vulkan) the stream.

#include <teserra/core/types.hpp>
#include <teserra/rhi/commands.hpp>
#include <teserra/rhi/format.hpp>
#include <teserra/rhi/handles.hpp>
#include <teserra/rhi/render_pass.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace teserra::rhi {

/// GPU work recorded for one submission. Devices own command lists per frame slot and hand them out with
/// Device::acquireCommandList(); a list can also be created on its own, e.g. by tests.
///
///     cmd.pushDebugGroup("Main pass");
///     cmd.beginRenderPass({.colorAttachments = {{.texture = backbuffer, .loadOp = LoadOp::Clear}}});
///     cmd.bindPipeline(pipeline);
///     cmd.pushConstants(DrawConstants{.color = red});
///     cmd.bindVertexBuffer(0, vertices);
///     cmd.draw({.vertexCount = 3});
///     cmd.endRenderPass();
///     cmd.popDebugGroup();
///
/// Recording rules, checked with TESERRA_ASSERT while recording:
/// - Render passes do not nest. Bindings, viewport, scissor and draws are recorded inside a render pass;
///   each pass starts with nothing bound. Copies are recorded outside render passes.
/// - A draw needs a bound pipeline; an indexed draw also needs a bound index buffer.
/// - Debug groups nest. A group opened inside a render pass is closed inside it, and a render pass cannot
///   close a group opened before it.
/// - Handles are not null, and counts stay within the k* limits of <teserra/rhi/limits.hpp>.
/// Checks that need the device, such as handle liveness and pipeline formats against the render pass,
/// happen at submit.
class CommandList final {
public:
    CommandList() = default;

    CommandList(const CommandList&) = delete;
    CommandList& operator=(const CommandList&) = delete;
    CommandList(CommandList&&) noexcept = default;
    CommandList& operator=(CommandList&&) noexcept = default;
    ~CommandList() = default;

    // Render passes

    /// Starts a render pass into the attachments of `desc`, which needs at least one attachment.
    void beginRenderPass(const RenderPassDesc& desc);
    void endRenderPass();

    // Bindings, inside a render pass

    /// Makes `pipeline` the one later draws use; inside a render pass.
    void bindPipeline(GraphicsPipelineHandle pipeline);
    /// Binds `group` at set `index` (< kMaxBindGroups), with one dynamic offset per dynamic uniform buffer
    /// in its layout (at most kMaxDynamicOffsets), in binding order. Each is a multiple of
    /// DeviceLimits::uniformBufferOffsetAlignment and is added to its binding's offset.
    void bindGroup(u32 index, BindGroupHandle group, std::span<const u32> dynamicOffsets = {});
    /// Binds `bindings` to consecutive vertex buffer slots from `firstSlot` on.
    void bindVertexBuffers(u32 firstSlot, std::span<const VertexBufferBinding> bindings);
    void bindVertexBuffer(u32 slot, BufferHandle buffer, u64 offset = 0);
    void bindIndexBuffer(BufferHandle buffer, IndexType indexType, u64 offset = 0);
    /// Writes `data` to the push constants from byte `offset` on, which later draws read
    /// (docs/architecture/rhi.md#push-constants). The offset and the size are multiples of 4, the size is
    /// above 0 and the bytes end within kMaxPushConstantSize; the bound pipeline's layout must cover them,
    /// which submit checks. Values last until changed, until the pass ends, or until a pipeline whose layout
    /// has a different push-constant range is bound.
    void pushConstants(u32 offset, std::span<const std::byte> data);
    /// pushConstants() with the bytes of `value`, e.g. a struct laid out like the shader's block.
    template <typename T>
        requires std::is_trivially_copyable_v<T>
    void pushConstants(const T& value, u32 offset = 0) {
        pushConstants(offset, std::as_bytes(std::span(&value, 1)));
    }
    /// Without a viewport or scissor, a render pass draws to its whole render area.
    void setViewport(const Viewport& viewport);
    void setScissor(const ScissorRect& scissor);

    // Draws, inside a render pass

    /// Draws with the bound pipeline and buffers; inside a render pass.
    void draw(const DrawArgs& args);
    void drawIndexed(const DrawIndexedArgs& args);

    // Copies, outside render passes

    /// Copies between buffers; outside render passes.
    void copyBuffer(const BufferCopy& copy);
    /// The texture needs TextureUsage::CopyDst and a color format: depth textures cannot be copied in v1.
    void copyBufferToTexture(const BufferTextureCopy& copy);
    /// Readback: the buffer receives the region's rows top row first. Once the work has completed (after
    /// waitIdle(), or once the frame slot is reused), a Readback buffer's map() holds them. The texture needs
    /// TextureUsage::CopySrc and a color format.
    void copyTextureToBuffer(const BufferTextureCopy& copy);

    // Debug groups, shown by graphics debuggers

    /// Opens a group labeled `label` (at most kMaxDebugLabelLength bytes) around the following commands.
    void pushDebugGroup(std::string_view label);
    void popDebugGroup();

    // For devices and backends

    /// Ends recording; Device::submit() calls it. No render pass or debug group may be open. A finished
    /// list cannot be recorded into until reset().
    void finish();
    /// Removes every command and starts recording again. The stream keeps its capacity.
    void reset() noexcept;

    [[nodiscard]] bool isFinished() const noexcept { return m_finished; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] u32 commandCount() const noexcept { return m_commandCount; }
    /// Bytes of the recorded stream.
    [[nodiscard]] usize sizeInBytes() const noexcept { return m_size; }
    /// Bytes the stream can hold before it has to grow.
    [[nodiscard]] usize capacityInBytes() const noexcept { return m_bytes.size(); }

    /// The recorded commands, in order.
    [[nodiscard]] CommandRange commands() const noexcept { return CommandRange({m_bytes.data(), m_size}); }

private:
    // Appends a record with `payloadSize` bytes of `payload` and room for `trailingSize` bytes after it;
    // returns the start of that room.
    std::byte* append(CommandType type, const void* payload, usize payloadSize, usize trailingSize);

    template <CommandPayload T>
    std::byte* append(const T& payload, usize trailingSize = 0) {
        return append(T::kType, &payload, sizeof(T), trailingSize);
    }

    void requireRecording() const;
    void requireInsideRenderPass(std::string_view command) const;
    void requireOutsideRenderPass(std::string_view command) const;

    std::vector<std::byte> m_bytes; // the stream is the first m_size bytes; the rest is capacity
    usize m_size = 0;
    u32 m_commandCount = 0;

    // Recording state for the checks; not part of the stream.
    u32 m_debugGroupDepth = 0;
    u32 m_renderPassDebugGroupDepth = 0; // m_debugGroupDepth when the open render pass began
    bool m_inRenderPass = false;
    bool m_pipelineBound = false;
    bool m_indexBufferBound = false;
    bool m_finished = false;
};

/// The commands of `list`, one per line, indented inside render passes and debug groups, e.g.
/// "Draw vertexCount=3 instanceCount=1 firstVertex=0 firstInstance=0". For debugging and tests.
[[nodiscard]] std::string describeCommands(const CommandList& list);

} // namespace teserra::rhi
