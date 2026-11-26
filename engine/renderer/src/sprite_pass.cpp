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
#include <limits>
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
    // Sprite materials read the sprite's texture at set 3; the layout is the same entry.
    TESERRA_TRY_ASSIGN(pass->m_drawLayout, device.createBindGroupLayout(
                                               {.entries = kMaterialEntries, .debugName = "Sprite texture"}));
    pass->m_frameLayout = frameLayout;
    const std::array groups{frameLayout, rhi::BindGroupLayoutHandle{}, pass->m_materialLayout};
    TESERRA_TRY_ASSIGN(pass->m_pipelineLayout,
                       device.createPipelineLayout({.groups = groups, .debugName = "Sprites"}));
    TESERRA_TRY_ASSIGN(pass->m_sampler, device.createSampler({.minFilter = config.filter,
                                                              .magFilter = config.filter,
                                                              .mipmapMode = rhi::MipmapMode::Nearest,
                                                              .debugName = "Sprites"}));
    TESERRA_TRY_ASSIGN(pass->m_tilemapSampler, device.createSampler({.minFilter = config.tilemapFilter,
                                                                     .magFilter = config.tilemapFilter,
                                                                     .mipmapMode = rhi::MipmapMode::Nearest,
                                                                     .debugName = "Tilemap chunks"}));

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
    : m_device(device), m_materialPipelines(device) {
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
    m_device.destroy(m_tilemapSampler);
    for (const MaterialPipelineLayout& layout : m_materialPipelineLayouts) {
        m_device.destroy(layout.layout);
    }
    m_device.destroy(m_pipelineLayout);
    m_device.destroy(m_drawLayout);
    m_device.destroy(m_materialLayout);
    m_device.destroy(m_fragmentShader);
    m_device.destroy(m_vertexShader);
}

void SpritePass::declare(PassBuilder& builder) {
    builder.writes(PassResource::SceneColor);
}

void SpritePass::forgetTexture(rhi::TextureHandle texture) {
    std::erase_if(m_textureGroups, [&](const TextureGroup& group) {
        if (group.texture != texture) {
            return false;
        }
        m_device.destroy(group.group);
        return true;
    });
}

void SpritePass::forgetMaterial(const Material& material) {
    m_materialPipelines.forget(material.vertexShader());
    m_materialPipelines.forget(material.fragmentShader());
}

rhi::PipelineLayoutHandle SpritePass::materialPipelineLayout(rhi::BindGroupLayoutHandle materialLayout) {
    const auto found =
        std::ranges::find(m_materialPipelineLayouts, materialLayout, &MaterialPipelineLayout::materialLayout);
    if (found != m_materialPipelineLayouts.end()) {
        return found->layout;
    }
    const std::array groups{m_frameLayout, rhi::BindGroupLayoutHandle{}, materialLayout, m_drawLayout};
    Result<rhi::PipelineLayoutHandle> created =
        m_device.createPipelineLayout({.groups = groups, .debugName = "Sprite material"});
    if (!created) {
        TESERRA_LOG_ERROR(LogCategory::Renderer, "Could not create a sprite material pipeline layout: {}",
                          created.error());
    }
    const rhi::PipelineLayoutHandle handle = created.value_or(rhi::PipelineLayoutHandle{});
    m_materialPipelineLayouts.push_back({.materialLayout = materialLayout, .layout = handle});
    return handle;
}

bool SpritePass::bindMaterialDraw(rhi::CommandList& commands, Bound& bound, const FrameTarget& target,
                                  const MaterialInstance& instance, rhi::TextureHandle texture,
                                  rhi::BufferHandle vertices, u64 vertexOffset) {
    const Material& material = instance.material();
    if (!material.layout().empty() && instance.group().isNull()) {
        return false; // a texture slot without a texture
    }
    const rhi::PipelineLayoutHandle layout = materialPipelineLayout(material.groupLayout());
    if (layout.isNull()) {
        return false;
    }
    const rhi::GraphicsPipelineHandle drawPipeline =
        m_materialPipelines.get({.vertexShader = material.vertexShader(),
                                 .fragmentShader = material.fragmentShader(),
                                 .layout = layout,
                                 .state = material.state(),
                                 .vertexLayout = {.buffers = std::span(&kSpriteVertexBuffer, 1),
                                                  .attributes = kSpriteVertexAttributes},
                                 .colorFormat = target.format,
                                 .depthFormat = target.depthFormat},
                                material.name());
    const rhi::BindGroupHandle drawGroup = textureGroup(texture, GroupKind::Draw);
    if (drawPipeline.isNull() || drawGroup.isNull()) {
        return false; // logged when creation failed
    }
    instance.flush();
    if (drawPipeline != bound.pipeline) {
        commands.bindPipeline(drawPipeline);
        bound.pipeline = drawPipeline;
    }
    if (!instance.group().isNull() && instance.group() != bound.group) {
        commands.bindGroup(rhi::kMaterialGroup, instance.group());
        bound.group = instance.group();
    }
    if (drawGroup != bound.drawGroup) {
        commands.bindGroup(rhi::kDrawGroup, drawGroup);
        bound.drawGroup = drawGroup;
    }
    if (vertices != bound.vertices) {
        commands.bindVertexBuffer(0, vertices, vertexOffset);
        bound.vertices = vertices;
    }
    return true;
}

rhi::GraphicsPipelineHandle SpritePass::pipeline(const FrameTarget& target, SpriteBlend blend) {
    const auto found = std::ranges::find_if(m_pipelines, [&](const Pipeline& cached) {
        return cached.format == target.format && cached.depthFormat == target.depthFormat &&
               cached.blend == blend;
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
        // Depth test and write stay off (the default): sprites are sorted back to front.
        .targets = target.layout(),
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
    m_pipelines.push_back(
        {.format = target.format, .depthFormat = target.depthFormat, .blend = blend, .pipeline = handle});
    return handle;
}

rhi::BindGroupHandle SpritePass::textureGroup(rhi::TextureHandle texture, GroupKind kind) {
    const auto found = std::ranges::find_if(m_textureGroups, [&](const TextureGroup& group) {
        return group.texture == texture && group.kind == kind;
    });
    if (found != m_textureGroups.end()) {
        return found->group;
    }
    const std::array entries{
        rhi::BindGroupEntry{.binding = 0,
                            .texture = texture,
                            .sampler = kind == GroupKind::Tilemap ? m_tilemapSampler : m_sampler}};
    Result<rhi::BindGroupHandle> created =
        m_device.createBindGroup({.layout = kind == GroupKind::Draw ? m_drawLayout : m_materialLayout,
                                  .entries = entries,
                                  .debugName = "Sprite texture"});
    if (!created) {
        TESERRA_LOG_ERROR(LogCategory::Renderer, "Could not create the bind group of sprite texture {}: {}",
                          texture, created.error());
        return {};
    }
    m_textureGroups.push_back({.texture = texture, .kind = kind, .group = *created});
    return *created;
}

bool SpritePass::bindDraw(rhi::CommandList& commands, Bound& bound, const FrameTarget& target,
                          SpriteBlend blend, rhi::TextureHandle texture, bool tilemap,
                          rhi::BufferHandle vertices, u64 vertexOffset) {
    const rhi::GraphicsPipelineHandle drawPipeline = pipeline(target, blend);
    const rhi::BindGroupHandle group =
        textureGroup(texture, tilemap ? GroupKind::Tilemap : GroupKind::Sprite);
    if (drawPipeline.isNull() || group.isNull()) {
        return false; // logged when creation failed
    }
    if (drawPipeline != bound.pipeline) {
        commands.bindPipeline(drawPipeline);
        bound.pipeline = drawPipeline;
    }
    if (group != bound.group) {
        commands.bindGroup(rhi::kMaterialGroup, group);
        bound.group = group;
    }
    if (vertices != bound.vertices) {
        commands.bindVertexBuffer(0, vertices, vertexOffset);
        bound.vertices = vertices;
    }
    return true;
}

void SpritePass::execute(PassContext& context, rhi::CommandList& commands) {
    const std::span<const SpriteDrawItem> sprites = context.packet().sprites;
    const std::span<const TilemapChunkDrawItem> chunks = context.packet().tilemapChunks;
    const std::span<const RenderView> views = context.views();
    m_stats = {.sprites = static_cast<u32>(sprites.size())};

    m_batcher.prepare(sprites);
    std::optional<UploadRing::Allocation> vertices;
    if (!sprites.empty()) {
        vertices = context.uploads().allocate(m_batcher.vertexBytes(), kVertexAlignment);
        if (vertices) {
            m_batcher.writeVertices(sprites, vertices->memory);
            m_stats.batches = static_cast<u32>(m_batcher.batches().size());
        }
    }
    // Chunks with quads by layer, keeping the order of the packet within a layer.
    m_chunkOrder.clear();
    for (u32 chunk = 0; chunk < chunks.size(); ++chunk) {
        if (chunks[chunk].quadCount > 0 && !chunks[chunk].vertexBuffer.isNull()) {
            m_chunkOrder.push_back(chunk);
        }
    }
    // The index breaks ties, so the order within a layer stays the packet's without std::stable_sort, which
    // may allocate (libstdc++ does for any size), and a steady-state frame must not.
    std::ranges::sort(m_chunkOrder, {}, [&](u32 chunk) { return std::pair{chunks[chunk].layer, chunk}; });
    m_stats.tilemapChunks = static_cast<u32>(m_chunkOrder.size());

    // Without sprite vertices (no sprites, or allocate() asserted) the batches are left out; chunks still
    // draw.
    const UploadRing::Allocation* spriteVertices = vertices ? &*vertices : nullptr;
    const std::span<const SpriteBatch> batches =
        spriteVertices != nullptr ? m_batcher.batches() : std::span<const SpriteBatch>{};
    const std::span<const u32> order = m_batcher.order();
    const bool anything = !batches.empty() || !m_chunkOrder.empty();

    // Begins a render pass into `target`; the frame's first pass into it clears it.
    m_clearedTargets.clear();
    const auto beginTarget = [&](const FrameTarget& target, const Color& clear) {
        const bool first = std::ranges::find(m_clearedTargets, target.texture) == m_clearedTargets.end();
        if (first) {
            m_clearedTargets.push_back(target.texture);
        }
        const rhi::LoadOp load = first ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
        commands.beginRenderPass({.colorAttachments = {{
                                      .texture = target.texture,
                                      .loadOp = load,
                                      .storeOp = rhi::StoreOp::Store,
                                      .clearColor = {clear.r, clear.g, clear.b, clear.a},
                                  }},
                                  .depthStencil = {.texture = target.depthTexture,
                                                   .depthLoadOp = load,
                                                   .depthStoreOp = rhi::StoreOp::Store}});
    };

    usize view = 0;
    while (view < views.size()) {
        // The views before runEnd render into the same target, in one render pass.
        const FrameTarget& target = context.viewTarget(view);
        usize runEnd = view + 1;
        while (runEnd < views.size() && context.viewTarget(runEnd).texture == target.texture) {
            ++runEnd;
        }
        beginTarget(target, views[view].clearColor);
        for (; view < runEnd; ++view) {
            if (anything) {
                drawView(context, commands, view, target, batches, order, spriteVertices, sprites, chunks);
            }
        }
        commands.endRenderPass();
    }
    // The composite pass shows the scene target, so it is cleared even when no view renders into it.
    const FrameTarget& scene = context.target(PassResource::SceneColor);
    if (std::ranges::find(m_clearedTargets, scene.texture) == m_clearedTargets.end()) {
        beginTarget(scene, Color{});
        commands.endRenderPass();
    }
}

void SpritePass::drawView(PassContext& context, rhi::CommandList& commands, usize view,
                          const FrameTarget& target, std::span<const SpriteBatch> batches,
                          std::span<const u32> order, const UploadRing::Allocation* spriteVertices,
                          std::span<const SpriteDrawItem> sprites,
                          std::span<const TilemapChunkDrawItem> chunks) {
    const rhi::Viewport area = viewportOf(context.views()[view], target.size);
    commands.setViewport(area);
    commands.setScissor({.x = static_cast<u32>(std::max(area.x, 0.0F)),
                         .y = static_cast<u32>(std::max(area.y, 0.0F)),
                         .width = static_cast<u32>(area.width),
                         .height = static_cast<u32>(area.height)});
    context.bindView(commands, view);
    commands.bindIndexBuffer(m_indices, rhi::IndexType::Uint16);
    Bound boundState;

    usize nextChunk = 0;
    // Draws the chunks not drawn yet whose layer is at most `layer`.
    const auto drawChunksUpTo = [&](i32 layer) {
        for (; nextChunk < m_chunkOrder.size() && chunks[m_chunkOrder[nextChunk]].layer <= layer;
             ++nextChunk) {
            const TilemapChunkDrawItem& chunk = chunks[m_chunkOrder[nextChunk]];
            if (chunk.texture != target.texture && bindDraw(commands, boundState, target, chunk.blend,
                                                            chunk.texture, true, chunk.vertexBuffer, 0)) {
                commands.drawIndexed({.indexCount = chunk.quadCount * 6});
                ++m_stats.drawCalls;
            }
        }
    };
    for (const SpriteBatch& batch : batches) {
        u32 first = batch.firstSprite;
        const u32 end = batch.firstSprite + batch.spriteCount;
        while (first < end) {
            drawChunksUpTo(sprites[order[first]].layer);
            // The sprites below the next chunk's layer draw together; without chunks left, the whole rest.
            u32 last = end;
            if (nextChunk < m_chunkOrder.size()) {
                const i16 chunkLayer = chunks[m_chunkOrder[nextChunk]].layer;
                last = first + 1;
                while (last < end && sprites[order[last]].layer < chunkLayer) {
                    ++last;
                }
            }
            // A sprite showing the target being drawn (a minimap seen by its own camera) is left out of it:
            // a texture cannot be sampled while it is rendered into.
            const bool bound =
                batch.texture != target.texture &&
                (batch.material != nullptr
                     ? bindMaterialDraw(commands, boundState, target, *batch.material, batch.texture,
                                        spriteVertices->buffer, spriteVertices->offset)
                     : bindDraw(commands, boundState, target, batch.blend, batch.texture, false,
                                spriteVertices->buffer, spriteVertices->offset));
            if (bound) {
                commands.drawIndexed(
                    {.indexCount = (last - first) * 6, .vertexOffset = static_cast<i32>(first * 4)});
                ++m_stats.drawCalls;
            }
            first = last;
        }
    }
    drawChunksUpTo(std::numeric_limits<i32>::max());
}

} // namespace teserra::renderer
