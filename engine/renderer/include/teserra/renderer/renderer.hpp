#pragma once

// The renderer: turns a FramePacket into RHI commands through an ordered list of passes
// (docs/architecture/renderer.md). It is built only on the RHI and never sees a backend, a window or the
// ECS.

#include <teserra/core/handle_pool.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/renderer/frame_packet.hpp>
#include <teserra/renderer/render_pass.hpp>
#include <teserra/renderer/render_target.hpp>
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
    /// The main scene target's size and how it is presented.
    SceneTargetConfig scene{};
};

class CompositePass;

/// Records frames with the passes added to it. Passes draw into the main scene target (color and
/// Depth32Float, PassResource::SceneColor) or the render targets of cameras; the renderer's own composite
/// pass runs after them and scales the scene target into the frame's output. Created, used and destroyed on
/// the device's thread, between the device's creation and destruction.
///
///     auto renderer = *renderer::Renderer::create(device, {.scene = {.resolution = {320, 180}}});
///     renderer->addPass(std::make_unique<SpritePass>(...));
///     // each frame, after device.beginFrame():
///     renderer->render(packet, {.texture = backbuffer, .format = format, .size = size}, commands);
class Renderer {
public:
    /// Creates the upload ring, the frame bind group (set 0) and the composite pass. The scene target is
    /// created by the first render().
    [[nodiscard]] static Result<std::unique_ptr<Renderer>> create(rhi::Device& device,
                                                                  const RendererConfig& config = {});
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    /// Adds a pass after the ones added before and calls its declare().
    void addPass(std::unique_ptr<RenderPass> pass);
    /// Adds an overlay pass, drawn into the frame's output after the composite pass, in the order overlays
    /// were added, so it is not scaled with the scene target (e.g. the debug UI). It may only declare
    /// PassResource::Output (asserted). Overlays also run when the scene target could not be created.
    void addOverlayPass(std::unique_ptr<RenderPass> pass);

    /// The layout of set 0 (rhi::kFrameGroup): the view's CameraData as a uniform buffer with a dynamic
    /// offset, seen by the vertex and fragment stages. Pipelines of passes use it at set 0 of their layout.
    [[nodiscard]] rhi::BindGroupLayoutHandle frameGroupLayout() const noexcept { return m_frameLayout; }

    /// Records one frame into `commands`, which the caller submits: (re)creates the scene target if `output`
    /// changed its size or format (SceneTargetConfig), starts the frame's upload memory, uploads the
    /// CameraData of every view, runs each pass inside a debug group named after it, and last the composite
    /// pass, which writes `output`. The App owns beginFrame(), submission and present; call render() between
    /// the device's beginFrame() and endFrame(). Views whose target was destroyed are left out (asserted). A
    /// steady-state frame allocates nothing.
    void render(const FramePacket& packet, const FrameTarget& output, rhi::CommandList& commands);

    /// Changes how the scene target is sized and presented, from the next render().
    void setSceneTarget(const SceneTargetConfig& config);
    [[nodiscard]] const SceneTargetConfig& sceneTarget() const noexcept { return m_sceneConfig; }
    /// The size of the main scene target for an output of `outputSize` pixels: what cameras of the scene
    /// target see.
    [[nodiscard]] UVec2 sceneSize(UVec2 outputSize) const noexcept {
        return sceneTargetSize(m_sceneConfig, outputSize);
    }
    /// Where the scene shows in an output of `outputSize` pixels (for mapping the mouse to scene pixels with
    /// outputToScene()).
    [[nodiscard]] CompositeRect composite(UVec2 outputSize) const noexcept {
        return compositeRect(sceneSize(outputSize), outputSize);
    }

    /// Creates a target for cameras (Camera::target): a color texture that can also be sampled, e.g. by
    /// sprites showing a minimap, and a depth texture unless `desc.depthFormat` is Undefined. Fails if a
    /// size is 0 or the device cannot create the textures.
    [[nodiscard]] Result<RenderTargetHandle> createRenderTarget(const RenderTargetDesc& desc);
    /// Destroys a target; the device keeps its textures until the frames that use them are done. Passes that
    /// sampled its color texture must forget it first (SpritePass::forgetTexture()). Null handles are
    /// ignored.
    void destroyRenderTarget(RenderTargetHandle target);
    /// The textures of `target`, or nullptr if it is not alive. Its `texture` can be sampled.
    [[nodiscard]] const FrameTarget* renderTarget(RenderTargetHandle target) const noexcept {
        return m_targets.tryGet(target);
    }
    /// The size of `target`, or of the main scene target for the null handle and an output of `outputSize`;
    /// {0, 0} if `target` is not alive.
    [[nodiscard]] UVec2 targetSize(RenderTargetHandle target, UVec2 outputSize) const noexcept;

private:
    struct Pass {
        std::unique_ptr<RenderPass> pass;
        PassBuilder declared;
    };

    Renderer(rhi::Device& device, UploadRing uploads, const SceneTargetConfig& scene);

    // Makes the scene target match the config and `output`; false if it could not be created.
    [[nodiscard]] bool updateSceneTarget(const FrameTarget& output);
    void destroyTextures(FrameTarget& target) noexcept;

    rhi::Device& m_device;
    UploadRing m_uploads;
    SceneTargetConfig m_sceneConfig;
    FrameTarget m_scene;
    bool m_sceneTargetFailed = false;
    HandlePool<FrameTarget, RenderTargetTag> m_targets;
    std::unique_ptr<CompositePass> m_composite;
    PassBuilder m_compositeDeclared;
    rhi::BindGroupLayoutHandle m_frameLayout;
    rhi::BindGroupHandle m_frameGroup;
    std::vector<Pass> m_passes;
    std::vector<Pass> m_overlays;
    // The frame's views in render order and the ring offset of each one's CameraData; reused every frame.
    std::vector<RenderView> m_views;
    std::vector<u32> m_viewOffsets;
    // The target of each view in m_views.
    std::vector<FrameTarget> m_viewTargets;
};

} // namespace teserra::renderer
