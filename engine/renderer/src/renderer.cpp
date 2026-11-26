#include <teserra/renderer/renderer.hpp>

#include <teserra/core/assert.hpp>
#include <teserra/core/log.hpp>
#include <teserra/core/profile.hpp>
#include <teserra/renderer/camera_data.hpp>
#include <teserra/rhi/binding.hpp>
#include <teserra/rhi/limits.hpp>
#include <teserra/rhi/render_pass.hpp>
#include <teserra/rhi/shader.hpp>
#include <teserra/rhi/texture.hpp>

#include "composite_pass.hpp"

#include <array>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace teserra::renderer {

namespace {

std::string_view resourceName(PassResource resource) noexcept {
    switch (resource) {
        case PassResource::SceneColor:
            return "SceneColor";
        case PassResource::Output:
            return "Output";
    }
    return "unknown";
}

// Sorts by order and keeps equal orders in packet order. Frames have a few views, and std::stable_sort may
// allocate, which a steady-state frame must not.
void sortByOrder(std::vector<RenderView>& views) noexcept {
    for (usize i = 1; i < views.size(); ++i) {
        for (usize j = i; j > 0 && views[j - 1].order > views[j].order; --j) {
            std::swap(views[j - 1], views[j]);
        }
    }
}

// Creates the textures of a target of `size`: color (rendered into and sampled) and, unless `depthFormat` is
// Undefined, depth. On failure nothing is left behind.
Result<FrameTarget> createTargetTextures(rhi::Device& device, UVec2 size, rhi::Format colorFormat,
                                         rhi::Format depthFormat, std::string_view name) {
    const std::string colorName = std::format("{} color", name);
    TESERRA_TRY_ASSIGN(
        const rhi::TextureHandle color,
        device.createTexture({.format = colorFormat,
                              .width = size.x,
                              .height = size.y,
                              .usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled,
                              .debugName = colorName}));
    FrameTarget target{.texture = color, .format = colorFormat, .size = size};
    if (depthFormat != rhi::Format::Undefined) {
        const std::string depthName = std::format("{} depth", name);
        Result<rhi::TextureHandle> depth = device.createTexture({.format = depthFormat,
                                                                 .width = size.x,
                                                                 .height = size.y,
                                                                 .usage = rhi::TextureUsage::DepthStencil,
                                                                 .debugName = depthName});
        if (!depth) {
            device.destroy(color);
            return Unexpected(std::move(depth).error());
        }
        target.depthTexture = *depth;
        target.depthFormat = depthFormat;
    }
    return target;
}

} // namespace

const FrameTarget& PassContext::target(PassResource resource) const {
    TESERRA_ASSERT(m_declared->usesResource(resource), "Pass '{}' uses {} without declaring it in declare()",
                   m_passName, resourceName(resource));
    return resource == PassResource::Output ? *m_output : *m_scene;
}

const FrameTarget& PassContext::viewTarget(usize view) const {
    TESERRA_ASSERT(m_declared->usesResource(PassResource::SceneColor),
                   "Pass '{}' uses the target of a view without declaring SceneColor in declare()",
                   m_passName);
    TESERRA_ASSERT(view < m_viewTargets.size(), "Pass '{}' uses the target of view {} of {}", m_passName,
                   view, m_viewTargets.size());
    return view < m_viewTargets.size() ? m_viewTargets[view] : *m_scene;
}

void PassContext::bindView(rhi::CommandList& commands, usize view) const {
    TESERRA_ASSERT(view < m_viewOffsets.size(), "Pass '{}' binds view {} of {}", m_passName, view,
                   m_viewOffsets.size());
    if (view < m_viewOffsets.size()) {
        commands.bindGroup(rhi::kFrameGroup, m_frameGroup, m_viewOffsets.subspan(view, 1));
    }
}

Result<std::unique_ptr<Renderer>> Renderer::create(rhi::Device& device, const RendererConfig& config) {
    TESERRA_TRY_ASSIGN(UploadRing uploads, UploadRing::create(device, config.uploadBytesPerFrame));
    // The constructor is private, so make_unique cannot reach it.
    std::unique_ptr<Renderer> renderer(
        new Renderer(device, std::move(uploads), config.scene)); // NOLINT(cppcoreguidelines-owning-memory)

    constexpr std::array kFrameEntries{rhi::BindGroupLayoutEntry{
        .binding = 0,
        .type = rhi::BindingType::UniformBuffer,
        .visibility = rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
        .hasDynamicOffset = true,
    }};
    TESERRA_TRY_ASSIGN(renderer->m_frameLayout,
                       device.createBindGroupLayout({.entries = kFrameEntries, .debugName = "Frame"}));
    // One group over the whole ring: each view binds it with its CameraData's offset.
    const std::array frameResources{rhi::BindGroupEntry{
        .binding = 0,
        .buffer = {.buffer = renderer->m_uploads.buffer(), .size = sizeof(CameraData)},
    }};
    TESERRA_TRY_ASSIGN(renderer->m_frameGroup, device.createBindGroup({.layout = renderer->m_frameLayout,
                                                                       .entries = frameResources,
                                                                       .debugName = "Frame"}));
    TESERRA_TRY_ASSIGN(renderer->m_composite, CompositePass::create(device));
    renderer->m_composite->setLetterboxColor(config.scene.letterboxColor);
    renderer->m_composite->declare(renderer->m_compositeDeclared);
    return renderer;
}

Renderer::Renderer(rhi::Device& device, UploadRing uploads, const SceneTargetConfig& scene)
    : m_device(device), m_uploads(std::move(uploads)), m_sceneConfig(scene) {
}

Renderer::~Renderer() {
    m_passes.clear(); // passes may own GPU objects of this device
    m_composite.reset();
    m_targets.forEach([&](RenderTargetHandle /*handle*/, FrameTarget& target) { destroyTextures(target); });
    destroyTextures(m_scene);
    m_device.destroy(m_frameGroup);
    m_device.destroy(m_frameLayout);
}

void Renderer::destroyTextures(FrameTarget& target) noexcept {
    m_device.destroy(target.texture);
    m_device.destroy(target.depthTexture);
    target = {};
}

void Renderer::addPass(std::unique_ptr<RenderPass> pass) {
    TESERRA_ASSERT(pass != nullptr, "Renderer::addPass with a null pass");
    if (pass == nullptr) {
        return;
    }
    TESERRA_ASSERT(pass->name().size() <= rhi::kMaxDebugLabelLength,
                   "Render pass name '{}' has more than {} characters", pass->name(),
                   rhi::kMaxDebugLabelLength);
    Pass& added = m_passes.emplace_back(Pass{.pass = std::move(pass), .declared = {}});
    added.pass->declare(added.declared);
}

void Renderer::addOverlayPass(std::unique_ptr<RenderPass> pass) {
    TESERRA_ASSERT(pass != nullptr, "Renderer::addOverlayPass with a null pass");
    if (pass == nullptr) {
        return;
    }
    TESERRA_ASSERT(pass->name().size() <= rhi::kMaxDebugLabelLength,
                   "Render pass name '{}' has more than {} characters", pass->name(),
                   rhi::kMaxDebugLabelLength);
    Pass& added = m_overlays.emplace_back(Pass{.pass = std::move(pass), .declared = {}});
    added.pass->declare(added.declared);
    TESERRA_ASSERT(!added.declared.usesResource(PassResource::SceneColor),
                   "Overlay pass '{}' declares SceneColor; overlays only draw into the output",
                   added.pass->name());
}

void Renderer::setSceneTarget(const SceneTargetConfig& config) {
    m_sceneConfig = config;
    m_sceneTargetFailed = false; // a new size may work
    m_composite->setLetterboxColor(config.letterboxColor);
}

bool Renderer::updateSceneTarget(const FrameTarget& output) {
    const UVec2 size = sceneSize(output.size);
    if (!m_scene.texture.isNull() && m_scene.size == size && m_scene.format == output.format) {
        return true;
    }
    if (m_sceneTargetFailed && m_scene.size == size) {
        return false; // logged when it failed; another size or setSceneTarget() tries again
    }
    // The device keeps the old textures until the frames in flight that use them are done.
    destroyTextures(m_scene);
    if (size.x == 0 || size.y == 0) {
        return false;
    }
    // The output's format, so that the composite copies one to one and passes need one pipeline per format.
    Result<FrameTarget> created =
        createTargetTextures(m_device, size, output.format, rhi::Format::Depth32Float, "Scene");
    if (!created) {
        TESERRA_LOG_ERROR(LogCategory::Renderer, "Could not create the {}x{} scene target: {}", size.x,
                          size.y, created.error());
        m_sceneTargetFailed = true;
        m_scene.size = size;
        return false;
    }
    m_scene = *created;
    m_sceneTargetFailed = false;
    TESERRA_LOG_DEBUG(LogCategory::Renderer, "Created the {}x{} scene target", size.x, size.y);
    return true;
}

Result<RenderTargetHandle> Renderer::createRenderTarget(const RenderTargetDesc& desc) {
    if (desc.size.x == 0 || desc.size.y == 0) {
        return Unexpected(Error{.code = ErrorCode::InvalidArgument,
                                .module = Module::Renderer,
                                .message = std::format("Render target '{}' has a size of {}x{}",
                                                       desc.debugName, desc.size.x, desc.size.y)});
    }
    const std::string_view name = desc.debugName.empty() ? std::string_view{"Render target"} : desc.debugName;
    TESERRA_TRY_ASSIGN(const FrameTarget target,
                       createTargetTextures(m_device, desc.size, desc.colorFormat, desc.depthFormat, name));
    return m_targets.create(target);
}

void Renderer::destroyRenderTarget(RenderTargetHandle target) {
    if (target.isNull()) {
        return;
    }
    FrameTarget* const found = m_targets.tryGet(target);
    TESERRA_ASSERT(found != nullptr, "Destroying stale render target {}", target);
    if (found != nullptr) {
        destroyTextures(*found);
        m_targets.destroy(target);
    }
}

UVec2 Renderer::targetSize(RenderTargetHandle target, UVec2 outputSize) const noexcept {
    if (target.isNull()) {
        return sceneSize(outputSize);
    }
    const FrameTarget* const found = m_targets.tryGet(target);
    return found != nullptr ? found->size : UVec2{0U};
}

void Renderer::render(const FramePacket& packet, const FrameTarget& output, rhi::CommandList& commands) {
    TESERRA_PROFILE_FUNCTION();
    m_uploads.beginFrame();
    const auto run = [&](PassContext& context, RenderPass& pass, const PassBuilder& declared) {
        TESERRA_PROFILE_SCOPE_DYNAMIC(pass.name());
        context.m_declared = &declared;
        context.m_passName = pass.name();
        commands.pushDebugGroup(context.m_passName);
        pass.execute(context, commands);
        commands.popDebugGroup();
    };
    if (!updateSceneTarget(output)) {
        // Nothing to draw into: the output shows the letterbox color, under the overlays.
        const Color clear = m_sceneConfig.letterboxColor;
        commands.beginRenderPass(
            {.colorAttachments = {{.texture = output.texture,
                                   .loadOp = rhi::LoadOp::Clear,
                                   .clearColor = {clear.r, clear.g, clear.b, clear.a}}}});
        commands.endRenderPass();
        PassContext context;
        context.m_device = &m_device;
        context.m_packet = &packet;
        context.m_uploads = &m_uploads;
        context.m_output = &output;
        for (const Pass& overlay : m_overlays) {
            run(context, *overlay.pass, overlay.declared);
        }
        return;
    }

    m_views.assign(packet.views.begin(), packet.views.end());
    sortByOrder(m_views);
    // Views of destroyed targets are left out; every other view gets its target.
    std::erase_if(m_views, [&](const RenderView& view) {
        const bool alive = view.target.isNull() || m_targets.contains(view.target);
        TESERRA_ASSERT(alive, "A view renders into destroyed render target {}", view.target);
        return !alive;
    });
    m_viewTargets.clear();
    for (const RenderView& view : m_views) {
        m_viewTargets.push_back(view.target.isNull() ? m_scene : m_targets.get(view.target));
    }

    // Every view's camera block at an offset the device can bind.
    const u64 alignment = m_device.limits().uniformBufferOffsetAlignment;
    m_viewOffsets.clear();
    for (const RenderView& view : m_views) {
        const std::optional<UploadRing::Allocation> camera =
            m_uploads.allocate(sizeof(CameraData), alignment);
        if (!camera) {
            m_viewOffsets.push_back(0); // allocate() asserted; the view keeps an offset so indices stay valid
            continue;
        }
        std::memcpy(camera->memory.data(), &view.camera, sizeof(CameraData));
        m_viewOffsets.push_back(static_cast<u32>(camera->offset)); // the ring is smaller than 4 GiB
    }

    PassContext context;
    context.m_device = &m_device;
    context.m_packet = &packet;
    context.m_uploads = &m_uploads;
    context.m_views = m_views;
    context.m_viewOffsets = m_viewOffsets;
    context.m_frameGroup = m_frameGroup;
    context.m_scene = &m_scene;
    context.m_output = &output;
    context.m_viewTargets = m_viewTargets;
    for (const Pass& pass : m_passes) {
        run(context, *pass.pass, pass.declared);
    }
    run(context, *m_composite, m_compositeDeclared); // it presents the scene target
    for (const Pass& overlay : m_overlays) {
        run(context, *overlay.pass, overlay.declared);
    }
}

} // namespace teserra::renderer
