#include <teserra/renderer/debug_pass.hpp>

#include <teserra/core/log.hpp>
#include <teserra/renderer/debug_draw.hpp>
#include <teserra/renderer/upload_ring.hpp>
#include <teserra/rhi/command_list.hpp>
#include <teserra/rhi/pipeline.hpp>
#include <teserra/rhi/render_pass.hpp>
#include <teserra/rhi/shader.hpp>

#include <teserra_renderer/shaders.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>

namespace teserra::renderer {

namespace {

constexpr rhi::VertexBufferLayout kDebugVertexBuffer{.stride = sizeof(DebugVertex)};
constexpr std::array<rhi::VertexAttribute, 2> kDebugVertexAttributes{{
    {.location = 0, .format = rhi::VertexFormat::Float3, .offset = 0},
    {.location = 1, .format = rhi::VertexFormat::Unorm8x4, .offset = 12},
}};
constexpr u64 kVertexAlignment = 16;

} // namespace

Result<std::unique_ptr<DebugPass>> DebugPass::create(rhi::Device& device,
                                                     rhi::BindGroupLayoutHandle frameLayout) {
    // The constructor is private, so make_unique cannot reach it.
    std::unique_ptr<DebugPass> pass(new DebugPass(device)); // NOLINT(cppcoreguidelines-owning-memory)
    TESERRA_TRY_ASSIGN(pass->m_vertexShader, device.createShaderModule({.stage = rhi::ShaderStage::Vertex,
                                                                        .blob = shaders::kDebugLinesVert,
                                                                        .debugName = "debug_lines vertex"}));
    TESERRA_TRY_ASSIGN(pass->m_fragmentShader,
                       device.createShaderModule({.stage = rhi::ShaderStage::Fragment,
                                                  .blob = shaders::kDebugLinesFrag,
                                                  .debugName = "debug_lines fragment"}));
    const std::array groups{frameLayout};
    TESERRA_TRY_ASSIGN(pass->m_pipelineLayout,
                       device.createPipelineLayout({.groups = groups, .debugName = "Debug lines"}));
    return pass;
}

DebugPass::~DebugPass() {
    for (const Pipeline& pipeline : m_pipelines) {
        m_device.destroy(pipeline.pipeline);
    }
    m_device.destroy(m_pipelineLayout);
    m_device.destroy(m_fragmentShader);
    m_device.destroy(m_vertexShader);
}

void DebugPass::declare(PassBuilder& builder) {
    builder.reads(PassResource::SceneColor);
    builder.writes(PassResource::SceneColor);
}

rhi::GraphicsPipelineHandle DebugPass::pipeline(const FrameTarget& target) {
    const auto found = std::ranges::find_if(m_pipelines, [&](const Pipeline& cached) {
        return cached.format == target.format && cached.depthFormat == target.depthFormat;
    });
    if (found != m_pipelines.end()) {
        return found->pipeline;
    }
    Result<rhi::GraphicsPipelineHandle> created = m_device.createGraphicsPipeline({
        .vertexShader = m_vertexShader,
        .fragmentShader = m_fragmentShader,
        .layout = m_pipelineLayout,
        .vertexBuffers = std::span(&kDebugVertexBuffer, 1),
        .vertexAttributes = kDebugVertexAttributes,
        .topology = rhi::PrimitiveTopology::LineList,
        .colorBlend = {{.enabled = true,
                        .srcColor = rhi::BlendFactor::SrcAlpha,
                        .dstColor = rhi::BlendFactor::OneMinusSrcAlpha,
                        .srcAlpha = rhi::BlendFactor::One,
                        .dstAlpha = rhi::BlendFactor::OneMinusSrcAlpha}},
        // No depth test yet: nothing in 2D writes depth.
        .targets = target.layout(),
        .debugName = "debug_lines",
    });
    if (!created) {
        TESERRA_LOG_ERROR(LogCategory::Renderer, "Could not create the debug line pipeline: {}",
                          created.error());
    } else {
        TESERRA_LOG_DEBUG(LogCategory::Renderer, "Created the debug line pipeline");
    }
    const rhi::GraphicsPipelineHandle handle = created.value_or(rhi::GraphicsPipelineHandle{});
    m_pipelines.push_back({.format = target.format, .depthFormat = target.depthFormat, .pipeline = handle});
    return handle;
}

void DebugPass::execute(PassContext& context, rhi::CommandList& commands) {
    const std::span<const DebugVertex> vertices = context.packet().debugLines;
    m_stats = {.lines = static_cast<u32>(vertices.size() / 2)};
    if (vertices.size() < 2 || context.views().empty()) {
        return;
    }
    const std::span<const std::byte> bytes = std::as_bytes(vertices.first(vertices.size() & ~usize{1}));
    const std::optional<UploadRing::Allocation> upload =
        context.uploads().allocate(bytes.size(), kVertexAlignment);
    if (!upload) {
        return;
    }
    std::ranges::copy(bytes, upload->memory.begin());

    const std::span<const RenderView> views = context.views();
    usize view = 0;
    while (view < views.size()) {
        // The views before runEnd render into the same target, in one render pass.
        const FrameTarget& target = context.viewTarget(view);
        usize runEnd = view + 1;
        while (runEnd < views.size() && context.viewTarget(runEnd).texture == target.texture) {
            ++runEnd;
        }
        const rhi::GraphicsPipelineHandle linePipeline = pipeline(target);
        if (linePipeline.isNull()) {
            view = runEnd; // logged when creation failed
            continue;
        }
        commands.beginRenderPass({.colorAttachments = {{.texture = target.texture,
                                                        .loadOp = rhi::LoadOp::Load,
                                                        .storeOp = rhi::StoreOp::Store}},
                                  .depthStencil = {.texture = target.depthTexture,
                                                   .depthLoadOp = rhi::LoadOp::Load,
                                                   .depthStoreOp = rhi::StoreOp::Store}});
        for (; view < runEnd; ++view) {
            const Vec2 size{static_cast<f32>(target.size.x), static_cast<f32>(target.size.y)};
            const Vec2 position = views[view].viewport.position * size;
            const Vec2 extent = views[view].viewport.size * size;
            commands.setViewport({.x = position.x, .y = position.y, .width = extent.x, .height = extent.y});
            commands.setScissor({.x = static_cast<u32>(std::max(position.x, 0.0F)),
                                 .y = static_cast<u32>(std::max(position.y, 0.0F)),
                                 .width = static_cast<u32>(extent.x),
                                 .height = static_cast<u32>(extent.y)});
            commands.bindPipeline(linePipeline);
            context.bindView(commands, view);
            commands.bindVertexBuffer(0, upload->buffer, upload->offset);
            commands.draw({.vertexCount = static_cast<u32>(bytes.size() / sizeof(DebugVertex))});
            ++m_stats.drawCalls;
        }
        commands.endRenderPass();
    }
}

} // namespace teserra::renderer
