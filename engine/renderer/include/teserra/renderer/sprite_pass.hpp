#pragma once

// The sprite pass (docs/architecture/renderer.md#sprite-batcher): draws FramePacket::sprites with the
// built-in sprite_unlit material, one draw call per batch of SpriteBatcher.

#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/renderer/render_pass.hpp>
#include <teserra/renderer/sprite_batcher.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/format.hpp>
#include <teserra/rhi/handles.hpp>
#include <teserra/rhi/sampler.hpp>

#include <memory>
#include <string_view>
#include <vector>

namespace teserra::renderer {

/// Creation parameters of the sprite pass.
struct SpritePassConfig {
    /// How sprite textures are filtered when scaled; pixel art wants Nearest.
    rhi::Filter filter = rhi::Filter::Linear;
};

/// What the last frame drew, for counters and tests.
struct SpritePassStats {
    u32 sprites = 0;
    u32 batches = 0;
    u32 drawCalls = 0;
};

/// Clears SceneColor to the first view's clear color and draws the frame's sprites for every view, in
/// SpriteBatcher order. Pipelines (one per target format and blend mode) and the bind group of each texture
/// are created when first needed and kept, so a steady-state frame creates no GPU objects and allocates
/// nothing.
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

    [[nodiscard]] std::string_view name() const override { return "Sprites"; }
    void declare(PassBuilder& builder) override;
    void execute(PassContext& context, rhi::CommandList& commands) override;

    /// Destroys the bind group kept for `texture`. Call it before destroying a texture that sprites used.
    void forgetTexture(rhi::TextureHandle texture);
    [[nodiscard]] const SpritePassStats& stats() const noexcept { return m_stats; }

private:
    struct Pipeline {
        rhi::Format format = rhi::Format::Undefined;
        SpriteBlend blend = SpriteBlend::Alpha;
        rhi::GraphicsPipelineHandle pipeline;
    };
    struct TextureGroup {
        rhi::TextureHandle texture;
        rhi::BindGroupHandle group;
    };

    explicit SpritePass(rhi::Device& device);

    // The pipeline for `blend` into targets of `format`, created on first use; null if creation failed.
    [[nodiscard]] rhi::GraphicsPipelineHandle pipeline(rhi::Format format, SpriteBlend blend);
    // The material group (set 2) of `texture`, created on first use; null if creation failed.
    [[nodiscard]] rhi::BindGroupHandle textureGroup(rhi::TextureHandle texture);

    rhi::Device& m_device;
    SpriteBatcher m_batcher;
    rhi::ShaderModuleHandle m_vertexShader;
    rhi::ShaderModuleHandle m_fragmentShader;
    rhi::BindGroupLayoutHandle m_materialLayout;
    rhi::PipelineLayoutHandle m_pipelineLayout;
    rhi::SamplerHandle m_sampler;
    rhi::BufferHandle m_indices;
    std::vector<Pipeline> m_pipelines;
    std::vector<TextureGroup> m_textureGroups;
    SpritePassStats m_stats;
};

} // namespace teserra::renderer
