#include <teserra/renderer/sprite_pass.hpp>

#include <teserra/core/assert.hpp>
#include <teserra/core/log.hpp>
#include <teserra/renderer/upload_ring.hpp>
#include <teserra/rhi/binding.hpp>
#include <teserra/rhi/buffer.hpp>
#include <teserra/rhi/command_list.hpp>
#include <teserra/rhi/commands.hpp>
#include <teserra/rhi/pipeline.hpp>
#include <teserra/rhi/render_pass.hpp>
#include <teserra/rhi/shader.hpp>

#include <teserra_renderer/shaders.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace teserra::renderer {

namespace {

// The two triangles of a sprite quad, counter-clockwise for the corner order of
// SpriteBatcher::writeVertices().
constexpr std::array<u16, 6> kQuadCorners{0, 1, 2, 0, 2, 3};

// Vertex memory is bound as a vertex buffer; 16 bytes covers every vertex format's alignment.
constexpr u64 kVertexAlignment = 16;

std::string_view blendName(SpriteBlend blend) noexcept {
    switch (blend) {
        case SpriteBlend::Alpha:
            return "alpha";
        case SpriteBlend::Premultiplied:
            return "premultiplied";
        case SpriteBlend::Additive:
            return "additive";
    }
    return "unknown";
}

rhi::BlendState blendState(SpriteBlend blend) noexcept {
    using rhi::BlendFactor;
    rhi::BlendState state{.enabled = true};
    switch (blend) {
        case SpriteBlend::Alpha:
            state.srcColor = BlendFactor::SrcAlpha;
            state.dstColor = BlendFactor::OneMinusSrcAlpha;
            break;
        case SpriteBlend::Premultiplied:
            state.srcColor = BlendFactor::One;
            state.dstColor = BlendFactor::OneMinusSrcAlpha;
            break;
        case SpriteBlend::Additive:
            state.srcColor = BlendFactor::SrcAlpha;
            state.dstColor = BlendFactor::One;
            break;
    }
    // Alpha accumulates coverage the same way in every mode.
    state.srcAlpha = BlendFactor::One;
    state.dstAlpha = BlendFactor::OneMinusSrcAlpha;
    return state;
}

// The view's area of a target in pixels; the viewport rectangle is normalized.
rhi::Viewport viewportOf(const RenderView& view, UVec2 targetSize) noexcept {
    const Vec2 size{static_cast<f32>(targetSize.x), static_cast<f32>(targetSize.y)};
    const Vec2 position = view.viewport.position * size;
    const Vec2 extent = view.viewport.size * size;
    return {.x = position.x, .y = position.y, .width = extent.x, .height = extent.y};
}

} // namespace

Result<std::unique_ptr<SpritePass>> SpritePass::create(rhi::Device& device,
                                                       rhi::BindGroupLayoutHandle frameLayout,
                                                       const SpritePassConfig& config) {
    // The constructor is private, so make_unique cannot reach it.
    std::unique_ptr<SpritePass> pass(new SpritePass(device)); // NOLINT(cppcoreguidelines-owning-memory)

    TESERRA_TRY_ASSIGN(pass->m_vertexShader, device.createShaderModule({.stage = rhi::ShaderStage::Vertex,
                                                                        .blob = shaders::kSpriteUnlitVert,
                                                                        .debugName = "sprite_unlit vertex"}));
    TESERRA_TRY_ASSIGN(pass->m_fragmentShader,
                       device.createShaderModule({.stage = rhi::ShaderStage::Fragment,
                                                  .blob = shaders::kSpriteUnlitFrag,
                                                  .debugName = "sprite_unlit fragment"}));
    constexpr std::array kMaterialEntries{rhi::BindGroupLayoutEntry{
        .binding = 0, .type = rhi::BindingType::SampledTexture, .visibility = rhi::ShaderStage::Fragment}};
    TESERRA_TRY_ASSIGN(
        pass->m_materialLayout,
        device.createBindGroupLayout({.entries = kMaterialEntries, .debugName = "Sprite material"}));
    const std::array groups{frameLayout, rhi::BindGroupLayoutHandle{}, pass->m_materialLayout};
    TESERRA_TRY_ASSIGN(pass->m_pipelineLayout,
                       device.createPipelineLayout({.groups = groups, .debugName = "Sprites"}));
    TESERRA_TRY_ASSIGN(pass->m_sampler, device.createSampler({.minFilter = config.filter,
                                                              .magFilter = config.filter,
                                                              .mipmapMode = rhi::MipmapMode::Nearest,
                                                              .debugName = "Sprites"}));

    // The quad pattern of every sprite a batch may hold; draws pick theirs with a vertex offset.
    std::vector<u16> indices;
    indices.reserve(usize{kMaxSpritesPerBatch} * 6);
    for (u32 sprite = 0; sprite < kMaxSpritesPerBatch; ++sprite) {
        const auto first = static_cast<u16>(sprite * 4);
        for (const u16 corner : kQuadCorners) {
            indices.push_back(static_cast<u16>(first + corner));
        }
    }
    TESERRA_TRY_ASSIGN(pass->m_indices,
                       device.createBuffer({.sizeInBytes = indices.size() * sizeof(u16),
                                            .usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst,
                                            .debugName = "Sprite quad indices"}));
    device.writeBuffer(pass->m_indices, 0, std::as_bytes(std::span(indices)));
    return pass;
}

SpritePass::SpritePass(rhi::Device& device)
    : m_device(device) {
}

SpritePass::~SpritePass() {
    for (const TextureGroup& group : m_textureGroups) {
        m_device.destroy(group.group);
    }
    for (const Pipeline& pipeline : m_pipelines) {
        m_device.destroy(pipeline.pipeline);
    }
    m_device.destroy(m_indices);
    m_device.destroy(m_sampler);
    m_device.destroy(m_pipelineLayout);
    m_device.destroy(m_materialLayout);
    m_device.destroy(m_fragmentShader);
    m_device.destroy(m_vertexShader);
}

void SpritePass::declare(PassBuilder& builder) {
    builder.writes(PassResource::SceneColor);
}

void SpritePass::forgetTexture(rhi::TextureHandle texture) {
    const auto found = std::ranges::find(m_textureGroups, texture, &TextureGroup::texture);
    if (found != m_textureGroups.end()) {
        m_device.destroy(found->group);
        m_textureGroups.erase(found);
    }
}

rhi::GraphicsPipelineHandle SpritePass::pipeline(rhi::Format format, SpriteBlend blend) {
    const auto found = std::ranges::find_if(m_pipelines, [&](const Pipeline& cached) {
        return cached.format == format && cached.blend == blend;
    });
    if (found != m_pipelines.end()) {
        return found->pipeline;
    }
    Result<rhi::GraphicsPipelineHandle> created = m_device.createGraphicsPipeline({
        .vertexShader = m_vertexShader,
        .fragmentShader = m_fragmentShader,
        .layout = m_pipelineLayout,
        .vertexBuffers = std::span(&kSpriteVertexBuffer, 1),
        .vertexAttributes = kSpriteVertexAttributes,
        .colorBlend = {blendState(blend)},
        .targets = {.colorFormats = {format}},
        .debugName = "sprite_unlit",
    });
    if (!created) {
        TESERRA_LOG_ERROR(LogCategory::Renderer, "Could not create the {} sprite pipeline: {}",
                          blendName(blend), created.error());
    } else {
        // Pipelines are created during warm-up, never on the per-sprite path; the log shows when.
        TESERRA_LOG_DEBUG(LogCategory::Renderer, "Created the {} sprite pipeline", blendName(blend));
    }
    const rhi::GraphicsPipelineHandle handle = created.value_or(rhi::GraphicsPipelineHandle{});
    m_pipelines.push_back({.format = format, .blend = blend, .pipeline = handle});
    return handle;
}

rhi::BindGroupHandle SpritePass::textureGroup(rhi::TextureHandle texture) {
    const auto found = std::ranges::find(m_textureGroups, texture, &TextureGroup::texture);
    if (found != m_textureGroups.end()) {
        return found->group;
    }
    const std::array entries{rhi::BindGroupEntry{.binding = 0, .texture = texture, .sampler = m_sampler}};
    Result<rhi::BindGroupHandle> created = m_device.createBindGroup(
        {.layout = m_materialLayout, .entries = entries, .debugName = "Sprite texture"});
    if (!created) {
        TESERRA_LOG_ERROR(LogCategory::Renderer, "Could not create the bind group of sprite texture {}: {}",
                          texture, created.error());
        return {};
    }
    m_textureGroups.push_back({.texture = texture, .group = *created});
    return *created;
}

void SpritePass::execute(PassContext& context, rhi::CommandList& commands) {
    const std::span<const SpriteDrawItem> sprites = context.packet().sprites;
    const std::span<const RenderView> views = context.views();
    const FrameTarget& target = context.target(PassResource::SceneColor);
    m_stats = {.sprites = static_cast<u32>(sprites.size())};

    m_batcher.prepare(sprites);
    std::optional<UploadRing::Allocation> vertices;
    if (!sprites.empty()) {
        vertices = context.uploads().allocate(m_batcher.vertexBytes(), kVertexAlignment);
        if (vertices) {
            m_batcher.writeVertices(sprites, vertices->memory);
        }
    }

    const Color clear = views.empty() ? Color{} : views.front().clearColor;
    commands.beginRenderPass({.colorAttachments = {{
                                  .texture = target.texture,
                                  .loadOp = rhi::LoadOp::Clear,
                                  .storeOp = rhi::StoreOp::Store,
                                  .clearColor = {clear.r, clear.g, clear.b, clear.a},
                              }}});
    if (vertices) { // no sprites, or allocate() asserted: only the clear
        m_stats.batches = static_cast<u32>(m_batcher.batches().size());
        for (usize view = 0; view < views.size(); ++view) {
            const rhi::Viewport area = viewportOf(views[view], target.size);
            commands.setViewport(area);
            commands.setScissor({.x = static_cast<u32>(std::max(area.x, 0.0F)),
                                 .y = static_cast<u32>(std::max(area.y, 0.0F)),
                                 .width = static_cast<u32>(area.width),
                                 .height = static_cast<u32>(area.height)});
            context.bindView(commands, view);
            commands.bindVertexBuffer(0, vertices->buffer, vertices->offset);
            commands.bindIndexBuffer(m_indices, rhi::IndexType::Uint16);
            rhi::GraphicsPipelineHandle boundPipeline;
            rhi::BindGroupHandle boundGroup;
            for (const SpriteBatch& batch : m_batcher.batches()) {
                const rhi::GraphicsPipelineHandle batchPipeline = pipeline(target.format, batch.blend);
                const rhi::BindGroupHandle group = textureGroup(batch.texture);
                if (batchPipeline.isNull() || group.isNull()) {
                    continue; // logged when creation failed
                }
                if (batchPipeline != boundPipeline) {
                    commands.bindPipeline(batchPipeline);
                    boundPipeline = batchPipeline;
                }
                if (group != boundGroup) {
                    commands.bindGroup(rhi::kMaterialGroup, group);
                    boundGroup = group;
                }
                commands.drawIndexed({.indexCount = batch.spriteCount * 6,
                                      .vertexOffset = static_cast<i32>(batch.firstSprite * 4)});
                ++m_stats.drawCalls;
            }
        }
    }
    commands.endRenderPass();
}

} // namespace teserra::renderer
