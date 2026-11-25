#include "composite_pass.hpp"

#include <teserra/core/log.hpp>
#include <teserra/renderer/render_target.hpp>
#include <teserra/rhi/binding.hpp>
#include <teserra/rhi/command_list.hpp>
#include <teserra/rhi/commands.hpp>
#include <teserra/rhi/pipeline.hpp>
#include <teserra/rhi/render_pass.hpp>
#include <teserra/rhi/sampler.hpp>
#include <teserra/rhi/shader.hpp>

#include <teserra_renderer/shaders.hpp>

#include <algorithm>
#include <array>

namespace teserra::renderer {

Result<std::unique_ptr<CompositePass>> CompositePass::create(rhi::Device& device) {
    // The constructor is private, so make_unique cannot reach it.
    std::unique_ptr<CompositePass> pass(new CompositePass(device)); // NOLINT(cppcoreguidelines-owning-memory)
    TESERRA_TRY_ASSIGN(pass->m_vertexShader, device.createShaderModule({.stage = rhi::ShaderStage::Vertex,
                                                                        .blob = shaders::kCompositeVert,
                                                                        .debugName = "composite vertex"}));
    TESERRA_TRY_ASSIGN(pass->m_fragmentShader,
                       device.createShaderModule({.stage = rhi::ShaderStage::Fragment,
                                                  .blob = shaders::kCompositeFrag,
                                                  .debugName = "composite fragment"}));
    constexpr std::array kEntries{rhi::BindGroupLayoutEntry{
        .binding = 0, .type = rhi::BindingType::SampledTexture, .visibility = rhi::ShaderStage::Fragment}};
    TESERRA_TRY_ASSIGN(pass->m_groupLayout,
                       device.createBindGroupLayout({.entries = kEntries, .debugName = "Composite"}));
    const std::array groups{rhi::BindGroupLayoutHandle{}, rhi::BindGroupLayoutHandle{}, pass->m_groupLayout};
    TESERRA_TRY_ASSIGN(pass->m_pipelineLayout,
                       device.createPipelineLayout({.groups = groups, .debugName = "Composite"}));
    // Nearest: an integer factor shows every scene pixel as a square block of output pixels.
    TESERRA_TRY_ASSIGN(pass->m_sampler, device.createSampler({.minFilter = rhi::Filter::Nearest,
                                                              .magFilter = rhi::Filter::Nearest,
                                                              .mipmapMode = rhi::MipmapMode::Nearest,
                                                              .debugName = "Composite"}));
    return pass;
}

CompositePass::~CompositePass() {
    m_device.destroy(m_group);
    for (const Pipeline& pipeline : m_pipelines) {
        m_device.destroy(pipeline.pipeline);
    }
    m_device.destroy(m_sampler);
    m_device.destroy(m_pipelineLayout);
    m_device.destroy(m_groupLayout);
    m_device.destroy(m_fragmentShader);
    m_device.destroy(m_vertexShader);
}

void CompositePass::declare(PassBuilder& builder) {
    builder.reads(PassResource::SceneColor);
    builder.writes(PassResource::Output);
}

rhi::GraphicsPipelineHandle CompositePass::pipeline(rhi::Format format) {
    const auto found = std::ranges::find(m_pipelines, format, &Pipeline::format);
    if (found != m_pipelines.end()) {
        return found->pipeline;
    }
    Result<rhi::GraphicsPipelineHandle> created = m_device.createGraphicsPipeline({
        .vertexShader = m_vertexShader,
        .fragmentShader = m_fragmentShader,
        .layout = m_pipelineLayout,
        .targets = {.colorFormats = {format}},
        .debugName = "composite",
    });
    if (!created) {
        TESERRA_LOG_ERROR(LogCategory::Renderer, "Could not create the composite pipeline: {}",
                          created.error());
    } else {
        TESERRA_LOG_DEBUG(LogCategory::Renderer, "Created the composite pipeline");
    }
    const rhi::GraphicsPipelineHandle handle = created.value_or(rhi::GraphicsPipelineHandle{});
    m_pipelines.push_back({.format = format, .pipeline = handle});
    return handle;
}

rhi::BindGroupHandle CompositePass::sceneGroup(rhi::TextureHandle texture) {
    if (texture == m_groupTexture) {
        return m_group;
    }
    // The scene target was recreated (resized): the old group goes with the old texture. The device keeps
    // both until the frames that use them are done.
    m_device.destroy(m_group);
    m_group = {};
    m_groupTexture = texture;
    const std::array entries{rhi::BindGroupEntry{.binding = 0, .texture = texture, .sampler = m_sampler}};
    Result<rhi::BindGroupHandle> created =
        m_device.createBindGroup({.layout = m_groupLayout, .entries = entries, .debugName = "Composite"});
    if (!created) {
        TESERRA_LOG_ERROR(LogCategory::Renderer, "Could not create the composite bind group: {}",
                          created.error());
        return {};
    }
    m_group = *created;
    return m_group;
}

void CompositePass::execute(PassContext& context, rhi::CommandList& commands) {
    const FrameTarget& scene = context.target(PassResource::SceneColor);
    const FrameTarget& output = context.target(PassResource::Output);
    commands.beginRenderPass({.colorAttachments = {{
                                  .texture = output.texture,
                                  .loadOp = rhi::LoadOp::Clear,
                                  .storeOp = rhi::StoreOp::Store,
                                  .clearColor = {m_letterbox.r, m_letterbox.g, m_letterbox.b, m_letterbox.a},
                              }}});
    const CompositeRect rect = compositeRect(scene.size, output.size);
    const rhi::GraphicsPipelineHandle drawPipeline = pipeline(output.format);
    const rhi::BindGroupHandle group = sceneGroup(scene.texture);
    if (rect.size.x >= 1.0F && rect.size.y >= 1.0F && !drawPipeline.isNull() && !group.isNull()) {
        commands.setViewport(
            {.x = rect.position.x, .y = rect.position.y, .width = rect.size.x, .height = rect.size.y});
        // The rectangle lies inside the output: compositeRect() centers it, or shrinks it to fit.
        commands.setScissor({.x = static_cast<u32>(rect.position.x),
                             .y = static_cast<u32>(rect.position.y),
                             .width = static_cast<u32>(rect.size.x),
                             .height = static_cast<u32>(rect.size.y)});
        commands.bindPipeline(drawPipeline);
        commands.bindGroup(rhi::kMaterialGroup, group);
        commands.draw({.vertexCount = 3});
    }
    commands.endRenderPass();
}

} // namespace teserra::renderer
