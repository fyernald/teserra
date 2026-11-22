#pragma once

// The renderer: turns a FramePacket into RHI commands through an ordered list of passes
// (docs/architecture/renderer.md). It is built only on the RHI and never sees a backend, a window or the
// ECS.

#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/renderer/frame_packet.hpp>
#include <teserra/renderer/render_pass.hpp>
#include <teserra/renderer/upload_ring.hpp>
#include <teserra/rhi/command_list.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/handles.hpp>

#include <memory>
#include <vector>

namespace teserra::renderer {

/// Creation parameters of a renderer.
struct RendererConfig {
    /// Upload memory of each frame in flight: cameras, sprite vertices, uniforms.
    u64 uploadBytesPerFrame = u64{4} * 1024 * 1024;
};

/// Records frames with the passes added to it. Created, used and destroyed on the device's thread, between
/// the device's creation and destruction.
///
///     auto renderer = *renderer::Renderer::create(device);
///     renderer->addPass(std::make_unique<SpritePass>(...));
///     // each frame, after device.beginFrame():
///     renderer->render(packet, {.texture = backbuffer, .format = format, .size = size}, commands);
class Renderer {
public:
    /// Creates the upload ring and the frame bind group (set 0).
    [[nodiscard]] static Result<std::unique_ptr<Renderer>> create(rhi::Device& device,
                                                                  const RendererConfig& config = {});
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    /// Adds a pass after the ones added before and calls its declare().
    void addPass(std::unique_ptr<RenderPass> pass);

    /// The layout of set 0 (rhi::kFrameGroup): the view's CameraData as a uniform buffer with a dynamic
    /// offset, seen by the vertex and fragment stages. Pipelines of passes use it at set 0 of their layout.
    [[nodiscard]] rhi::BindGroupLayoutHandle frameGroupLayout() const noexcept { return m_frameLayout; }

    /// Records one frame into `commands`, which the caller submits: starts the frame's upload memory, uploads
    /// the CameraData of every view, then runs each pass inside a debug group named after it. The App owns
    /// beginFrame(), submission and present; call render() between the device's beginFrame() and endFrame().
    /// A steady-state frame allocates nothing.
    void render(const FramePacket& packet, const FrameTarget& target, rhi::CommandList& commands);

private:
    struct Pass {
        std::unique_ptr<RenderPass> pass;
        PassBuilder declared;
    };

    Renderer(rhi::Device& device, UploadRing uploads);

    rhi::Device& m_device;
    UploadRing m_uploads;
    rhi::BindGroupLayoutHandle m_frameLayout;
    rhi::BindGroupHandle m_frameGroup;
    std::vector<Pass> m_passes;
    // The frame's views in render order and the ring offset of each one's CameraData; reused every frame.
    std::vector<RenderView> m_views;
    std::vector<u32> m_viewOffsets;
};

} // namespace teserra::renderer
