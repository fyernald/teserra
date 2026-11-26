#pragma once

// Teserra's renderer backend for Dear ImGui (ADR-0013): an overlay pass of the renderer that draws a
// DebugUi's draw data into the frame's output through the RHI, never through a graphics API directly.
//
//   auto pass = ImGuiPass::create(device);
//   ImGuiPass& imgui = **pass;
//   renderer.addOverlayPass(std::move(pass).value());
//   // each frame, after DebugUi::endFrame():
//   imgui.setDrawData(debugUi.drawData());

#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/renderer/render_pass.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/handles.hpp>

#include <imgui.h>

#include <memory>
#include <string_view>
#include <vector>

namespace teserra::debugui {

/// What the pass drew in its last execute().
struct ImGuiPassStats {
    u32 drawCalls = 0;
    u32 vertices = 0;
    u32 indices = 0;
    /// Textures the pass created for ImGui and has alive (its font atlas, mostly).
    u32 textures = 0;
};

/// Draws ImDrawData into PassResource::Output: one indexed draw per ImDrawCmd with its clip rectangle as the
/// scissor, vertices and indices in the frame's upload ring, ImGui's colors decoded from sRGB when the output
/// is sRGB. ImGui's own textures (the font atlas) are created, updated and destroyed as its ImTextureData
/// asks; other textures (textureId()) get a bind group on first use, dropped when unused for a few frames.
class ImGuiPass final : public renderer::RenderPass {
public:
    [[nodiscard]] static Result<std::unique_ptr<ImGuiPass>> create(rhi::Device& device);
    /// Destroys the textures it created and its GPU objects.
    ~ImGuiPass() override;

    ImGuiPass(const ImGuiPass&) = delete;
    ImGuiPass& operator=(const ImGuiPass&) = delete;
    ImGuiPass(ImGuiPass&&) = delete;
    ImGuiPass& operator=(ImGuiPass&&) = delete;

    /// The draw data of the next execute(), from DebugUi::drawData(); nullptr draws nothing. It must stay
    /// valid until then; the pass forgets it after drawing.
    void setDrawData(ImDrawData* drawData) noexcept { m_drawData = drawData; }

    [[nodiscard]] std::string_view name() const override { return "Debug UI"; }
    void declare(renderer::PassBuilder& builder) override;
    void execute(renderer::PassContext& context, rhi::CommandList& commands) override;

    [[nodiscard]] const ImGuiPassStats& stats() const noexcept { return m_stats; }

private:
    struct Pipeline {
        rhi::Format format = rhi::Format::Undefined;
        rhi::GraphicsPipelineHandle pipeline{};
    };
    struct TextureGroup {
        rhi::TextureHandle texture{};
        rhi::BindGroupHandle group{};
        u64 lastUsedFrame = 0;
    };

    explicit ImGuiPass(rhi::Device& device) noexcept
        : m_device(device) {}

    // Creates, updates or destroys the GPU texture of `texture` as its status asks.
    void updateTexture(ImTextureData& texture);
    // The bind group that samples `texture`, created on first use; null if it cannot be made.
    [[nodiscard]] rhi::BindGroupHandle textureGroup(rhi::TextureHandle texture);
    [[nodiscard]] rhi::GraphicsPipelineHandle pipeline(rhi::Format format);
    // Drops the bind groups of textures not drawn for a few frames, and those of `destroyed`.
    void forgetTexture(rhi::TextureHandle destroyed);

    rhi::Device& m_device;
    ImDrawData* m_drawData = nullptr;
    rhi::ShaderModuleHandle m_vertexShader;
    rhi::ShaderModuleHandle m_fragmentShader;
    rhi::BindGroupLayoutHandle m_frameLayout;
    rhi::BindGroupLayoutHandle m_textureLayout;
    rhi::PipelineLayoutHandle m_pipelineLayout;
    rhi::SamplerHandle m_sampler;
    // Binds the upload ring's buffer, with the frame block's offset as a dynamic offset.
    rhi::BindGroupHandle m_frameGroup;
    std::vector<Pipeline> m_pipelines;
    std::vector<TextureGroup> m_textureGroups;
    // The textures created for ImGui's ImTextureData, destroyed with the pass.
    std::vector<rhi::TextureHandle> m_ownedTextures;
    u64 m_frame = 0;
    ImGuiPassStats m_stats;
};

} // namespace teserra::debugui
