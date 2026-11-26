#pragma once

// The sprite pass (docs/architecture/renderer.md#sprite-batcher): draws FramePacket::sprites with the
// built-in sprite_unlit material, one draw call per batch of SpriteBatcher.

#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/renderer/material.hpp>
#include <teserra/renderer/pipeline_cache.hpp>
#include <teserra/renderer/render_pass.hpp>
#include <teserra/renderer/sprite_batcher.hpp>
#include <teserra/renderer/upload_ring.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/format.hpp>
#include <teserra/rhi/handles.hpp>
#include <teserra/rhi/sampler.hpp>

#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace teserra::renderer {

/// Creation parameters of the sprite pass.
struct SpritePassConfig {
    /// How sprite textures are filtered when scaled; pixel art wants Nearest.
    rhi::Filter filter = rhi::Filter::Linear;
    /// How tilemap chunk textures are filtered. Nearest keeps neighboring tiles from bleeding into each other
    /// at any zoom.
    rhi::Filter tilemapFilter = rhi::Filter::Nearest;
};

/// What the last frame drew, for counters and tests.
struct SpritePassStats {
    u32 sprites = 0;
    u32 batches = 0;
    /// Tilemap chunks of the packet with quads.
    u32 tilemapChunks = 0;
    /// Sprite batches and tilemap chunks drawn, over all views.
    u32 drawCalls = 0;
};

/// Draws the frame's sprites for every view, in SpriteBatcher order, and its tilemap chunks: each before the
/// first sprite of its layer or a higher one. Consecutive views with the same target share a render pass; the
/// first pass into a target clears its color to the view's clear color and its depth to 1. The scene target
/// is cleared even when no view renders into it. Sprites and chunks whose texture is the target being drawn
/// (a minimap seen by its own camera) are left out of that target. Pipelines (one per target layout and blend
/// mode) and the bind group of each texture are created when first needed and kept, so a steady-state frame
/// creates no GPU objects and allocates nothing.
///
/// Sprites with a material (SpriteDrawItem::material) draw with its shaders and state, from a pipeline cache:
/// the frame's camera at set 0, the instance's group at set 2, and the sprite's texture at set 3 binding 0
/// (`layout(set = 3, binding = 0) uniform sampler2D spriteTexture;`), sampled with the sprite filter. Sprites
/// whose instance has a texture slot without a texture are left out.
class SpritePass final : public RenderPass {
public:
    /// `frameLayout` is Renderer::frameGroupLayout(), set 0 of the pass's pipelines.
    [[nodiscard]] static Result<std::unique_ptr<SpritePass>>
    create(rhi::Device& device, rhi::BindGroupLayoutHandle frameLayout, const SpritePassConfig& config = {});
    ~SpritePass() override;

    SpritePass(const SpritePass&) = delete;
    SpritePass& operator=(const SpritePass&) = delete;
    SpritePass(SpritePass&&) = delete;
    SpritePass& operator=(SpritePass&&) = delete;

    /// RenderPass interface: "Sprites", writing SceneColor.
    [[nodiscard]] std::string_view name() const override { return "Sprites"; }
    void declare(PassBuilder& builder) override;
    void execute(PassContext& context, rhi::CommandList& commands) override;

    /// Destroys the bind groups kept for `texture`. Call it before destroying a texture that sprites used.
    void forgetTexture(rhi::TextureHandle texture);
    /// Destroys the pipelines and the pipeline layout kept for `material`. Call it before the material
    /// unloads.
    void forgetMaterial(const Material& material);
    /// The layout of set 3 that sprite materials read the sprite's texture through.
    [[nodiscard]] rhi::BindGroupLayoutHandle drawGroupLayout() const noexcept { return m_drawLayout; }
    [[nodiscard]] const SpritePassStats& stats() const noexcept { return m_stats; }
    /// The pipelines created so far for the materials of sprites, one per material and target layout.
    [[nodiscard]] usize materialPipelineCount() const noexcept { return m_materialPipelines.size(); }

private:
    struct Pipeline {
        rhi::Format format = rhi::Format::Undefined;
        rhi::Format depthFormat = rhi::Format::Undefined;
        SpriteBlend blend = SpriteBlend::Alpha;
        rhi::GraphicsPipelineHandle pipeline;
    };
    // Which layout and sampler a texture's group has.
    enum class GroupKind : u8 {
        Sprite,  // set 2 of sprite_unlit, the sprite sampler
        Tilemap, // set 2 of sprite_unlit, the tilemap sampler
        Draw,    // set 3 of sprite materials, the sprite sampler
    };
    struct TextureGroup {
        rhi::TextureHandle texture;
        GroupKind kind = GroupKind::Sprite;
        rhi::BindGroupHandle group;
    };
    struct MaterialPipelineLayout {
        rhi::BindGroupLayoutHandle materialLayout; // may be null
        rhi::PipelineLayoutHandle layout;
    };
    // What is bound in the current view, so draws rebind only what changes.
    struct Bound {
        rhi::GraphicsPipelineHandle pipeline;
        rhi::BindGroupHandle group;
        rhi::BindGroupHandle drawGroup;
        rhi::BufferHandle vertices;
    };

    explicit SpritePass(rhi::Device& device);

    // The pipeline for `blend` into targets like `target`, created on first use; null if creation failed.
    [[nodiscard]] rhi::GraphicsPipelineHandle pipeline(const FrameTarget& target, SpriteBlend blend);
    // The group of `texture` of `kind`, created on first use; null if creation failed.
    [[nodiscard]] rhi::BindGroupHandle textureGroup(rhi::TextureHandle texture, GroupKind kind);
    // The pipeline layout of sprite materials with `materialLayout` at set 2, created on first use.
    [[nodiscard]] rhi::PipelineLayoutHandle materialPipelineLayout(rhi::BindGroupLayoutHandle materialLayout);
    // Binds what a draw with `material` needs; false if it cannot draw.
    [[nodiscard]] bool bindMaterialDraw(rhi::CommandList& commands, Bound& bound, const FrameTarget& target,
                                        const MaterialInstance& material, rhi::TextureHandle texture,
                                        rhi::BufferHandle vertices, u64 vertexOffset);
    // Binds what a draw needs; false if its pipeline or group could not be created.
    [[nodiscard]] bool bindDraw(rhi::CommandList& commands, Bound& bound, const FrameTarget& target,
                                SpriteBlend blend, rhi::TextureHandle texture, bool tilemap,
                                rhi::BufferHandle vertices, u64 vertexOffset);
    // Draws the batches and chunks of view `view` into `target`, inside the render pass of its target.
    void drawView(PassContext& context, rhi::CommandList& commands, usize view, const FrameTarget& target,
                  std::span<const SpriteBatch> batches, std::span<const u32> order,
                  const UploadRing::Allocation* spriteVertices, std::span<const SpriteDrawItem> sprites,
                  std::span<const TilemapChunkDrawItem> chunks);

    rhi::Device& m_device;
    SpriteBatcher m_batcher;
    rhi::BindGroupLayoutHandle m_frameLayout;
    rhi::BindGroupLayoutHandle m_drawLayout;
    std::vector<MaterialPipelineLayout> m_materialPipelineLayouts;
    PipelineCache m_materialPipelines;
    rhi::ShaderModuleHandle m_vertexShader;
    rhi::ShaderModuleHandle m_fragmentShader;
    rhi::BindGroupLayoutHandle m_materialLayout;
    rhi::PipelineLayoutHandle m_pipelineLayout;
    rhi::SamplerHandle m_sampler;
    rhi::SamplerHandle m_tilemapSampler;
    rhi::BufferHandle m_indices;
    std::vector<Pipeline> m_pipelines;
    std::vector<TextureGroup> m_textureGroups;
    // The packet's tilemap chunks with quads, ordered by layer; reused every frame.
    std::vector<u32> m_chunkOrder;
    // The targets cleared this frame; reused every frame.
    std::vector<rhi::TextureHandle> m_clearedTargets;
    SpritePassStats m_stats;
};

} // namespace teserra::renderer
