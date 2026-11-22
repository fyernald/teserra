#pragma once

// Render passes (docs/architecture/renderer.md#frame-structure). A pass declares the frame resources it reads
// and writes, then records its commands. In v1 passes run in the order they were added and the declarations
// guard against undeclared use; a render graph will order passes and insert barriers from the same
// declarations.

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/renderer/frame_packet.hpp>
#include <teserra/rhi/command_list.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/format.hpp>
#include <teserra/rhi/handles.hpp>

#include <cstddef>
#include <span>
#include <string_view>

namespace teserra::renderer {

class UploadRing;

/// The frame resources passes read and write. v1 has the frame's output; scene depth and offscreen targets
/// follow with render targets (#95).
enum class PassResource : u8 {
    /// The texture the frame is rendered into, usually the swapchain image.
    SceneColor,
};

/// The texture of a frame resource.
struct FrameTarget {
    rhi::TextureHandle texture{};
    rhi::Format format = rhi::Format::Undefined;
    UVec2 size{0U};
};

/// What a pass declares in RenderPass::declare().
class PassBuilder {
public:
    void reads(PassResource resource) noexcept { m_reads |= bit(resource); }
    void writes(PassResource resource) noexcept { m_writes |= bit(resource); }

    [[nodiscard]] bool readsResource(PassResource resource) const noexcept {
        return (m_reads & bit(resource)) != 0;
    }
    [[nodiscard]] bool writesResource(PassResource resource) const noexcept {
        return (m_writes & bit(resource)) != 0;
    }
    [[nodiscard]] bool usesResource(PassResource resource) const noexcept {
        return readsResource(resource) || writesResource(resource);
    }

private:
    [[nodiscard]] static constexpr u32 bit(PassResource resource) noexcept {
        return 1U << static_cast<u32>(resource);
    }

    u32 m_reads = 0;
    u32 m_writes = 0;
};

/// What a pass works with while it records, valid during RenderPass::execute().
class PassContext {
public:
    [[nodiscard]] rhi::Device& device() const noexcept { return *m_device; }
    /// Upload memory of the current frame, for vertices, indices and uniforms.
    [[nodiscard]] UploadRing& uploads() const noexcept { return *m_uploads; }
    /// The frame's views, sorted by RenderView::order.
    [[nodiscard]] std::span<const RenderView> views() const noexcept { return m_views; }
    [[nodiscard]] u32 frameSlot() const noexcept { return m_device->frameSlot(); }
    /// The texture of `resource`. Asserts that the pass declared it in declare().
    [[nodiscard]] const FrameTarget& target(PassResource resource) const;
    /// Binds the camera of view `view` (an index into views()) at set 0, rhi::kFrameGroup. Pipelines include
    /// Renderer::frameGroupLayout() at set 0 of their layout. Call it inside a render pass.
    void bindView(rhi::CommandList& commands, usize view) const;

private:
    friend class Renderer;

    PassContext() = default;

    rhi::Device* m_device = nullptr;
    UploadRing* m_uploads = nullptr;
    std::span<const RenderView> m_views{};
    std::span<const u32> m_viewOffsets{};
    rhi::BindGroupHandle m_frameGroup{};
    const FrameTarget* m_target = nullptr;
    const PassBuilder* m_declared = nullptr;
    std::string_view m_passName{};
};

/// A step of the frame, such as drawing the scene's sprites or debug shapes.
class RenderPass {
public:
    RenderPass() = default;
    virtual ~RenderPass() = default;

    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;
    RenderPass(RenderPass&&) = delete;
    RenderPass& operator=(RenderPass&&) = delete;

    /// Shown by graphics debuggers around the pass's commands and in messages; at most
    /// rhi::kMaxDebugLabelLength characters.
    [[nodiscard]] virtual std::string_view name() const = 0;
    /// Declares the resources the pass reads and writes; called once, when the pass is added.
    virtual void declare(PassBuilder& builder) = 0;
    /// Records the pass's commands for the frame. Render passes it begins must also end in it.
    virtual void execute(PassContext& context, rhi::CommandList& commands) = 0;
};

} // namespace teserra::renderer
