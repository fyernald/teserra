#include <teserra/renderer/renderer.hpp>

#include <teserra/core/assert.hpp>
#include <teserra/renderer/camera_data.hpp>
#include <teserra/rhi/binding.hpp>
#include <teserra/rhi/limits.hpp>
#include <teserra/rhi/shader.hpp>

#include <array>
#include <cstring>
#include <optional>
#include <span>
#include <utility>

namespace teserra::renderer {

namespace {

std::string_view resourceName(PassResource resource) noexcept {
    switch (resource) {
        case PassResource::SceneColor:
            return "SceneColor";
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

} // namespace

const FrameTarget& PassContext::target(PassResource resource) const {
    TESERRA_ASSERT(m_declared->usesResource(resource), "Pass '{}' uses {} without declaring it in declare()",
                   m_passName, resourceName(resource));
    return *m_target; // SceneColor is the only resource in v1
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
        new Renderer(device, std::move(uploads))); // NOLINT(cppcoreguidelines-owning-memory)

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
    return renderer;
}

Renderer::Renderer(rhi::Device& device, UploadRing uploads)
    : m_device(device), m_uploads(std::move(uploads)) {
}

Renderer::~Renderer() {
    m_passes.clear(); // passes may own GPU objects of this device
    m_device.destroy(m_frameGroup);
    m_device.destroy(m_frameLayout);
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

void Renderer::render(const FramePacket& packet, const FrameTarget& target, rhi::CommandList& commands) {
    m_uploads.beginFrame();
    m_views.assign(packet.views.begin(), packet.views.end());
    sortByOrder(m_views);

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
    context.m_uploads = &m_uploads;
    context.m_views = m_views;
    context.m_viewOffsets = m_viewOffsets;
    context.m_frameGroup = m_frameGroup;
    context.m_target = &target;
    for (const Pass& pass : m_passes) {
        context.m_declared = &pass.declared;
        context.m_passName = pass.pass->name();
        commands.pushDebugGroup(context.m_passName);
        pass.pass->execute(context, commands);
        commands.popDebugGroup();
    }
}

} // namespace teserra::renderer
