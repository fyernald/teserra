#pragma once

// The debug pass (docs/architecture/renderer.md#debug-draw): draws FramePacket::debugLines over what the
// passes before it drew, for every view, with the built-in debug_lines material.

#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/renderer/render_pass.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/format.hpp>
#include <teserra/rhi/handles.hpp>

#include <memory>
#include <string_view>
#include <vector>

namespace teserra::renderer {

/// What the last frame drew.
struct DebugPassStats {
    u32 lines = 0;
    u32 drawCalls = 0;
};

/// Loads the target of each view (keeps what is there) and draws the frame's debug lines, one draw call per
/// view; consecutive views with the same target share a render pass. The pipeline of each target layout is
/// created when first needed; a steady-state frame creates nothing and
/// allocates nothing. A frame without lines records nothing.
class DebugPass final : public RenderPass {
public:
    /// `frameLayout` is Renderer::frameGroupLayout(), set 0 of the pass's pipelines.
    [[nodiscard]] static Result<std::unique_ptr<DebugPass>> create(rhi::Device& device,
                                                                   rhi::BindGroupLayoutHandle frameLayout);
    ~DebugPass() override;

    DebugPass(const DebugPass&) = delete;
    DebugPass& operator=(const DebugPass&) = delete;
    DebugPass(DebugPass&&) = delete;
    DebugPass& operator=(DebugPass&&) = delete;

    [[nodiscard]] std::string_view name() const override { return "Debug"; }
    void declare(PassBuilder& builder) override;
    void execute(PassContext& context, rhi::CommandList& commands) override;

    [[nodiscard]] const DebugPassStats& stats() const noexcept { return m_stats; }

private:
    struct Pipeline {
        rhi::Format format = rhi::Format::Undefined;
        rhi::Format depthFormat = rhi::Format::Undefined;
        rhi::GraphicsPipelineHandle pipeline;
    };

    explicit DebugPass(rhi::Device& device) noexcept
        : m_device(device) {}

    [[nodiscard]] rhi::GraphicsPipelineHandle pipeline(const FrameTarget& target);

    rhi::Device& m_device;
    rhi::ShaderModuleHandle m_vertexShader;
    rhi::ShaderModuleHandle m_fragmentShader;
    rhi::PipelineLayoutHandle m_pipelineLayout;
    std::vector<Pipeline> m_pipelines;
    DebugPassStats m_stats;
};

} // namespace teserra::renderer
