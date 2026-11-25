#pragma once

// The composite pass (docs/architecture/renderer.md#render-targets): the last pass of every frame, which the
// renderer runs itself. It draws the main scene target into the frame's output, scaled by compositeRect(),
// and fills the rest with the letterbox color. It is the one place where presentation scaling happens.

#include <teserra/core/math.hpp>
#include <teserra/core/result.hpp>
#include <teserra/renderer/render_pass.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/format.hpp>
#include <teserra/rhi/handles.hpp>

#include <memory>
#include <string_view>
#include <vector>

namespace teserra::renderer {

class CompositePass final : public RenderPass {
public:
    [[nodiscard]] static Result<std::unique_ptr<CompositePass>> create(rhi::Device& device);
    ~CompositePass() override;

    CompositePass(const CompositePass&) = delete;
    CompositePass& operator=(const CompositePass&) = delete;
    CompositePass(CompositePass&&) = delete;
    CompositePass& operator=(CompositePass&&) = delete;

    [[nodiscard]] std::string_view name() const override { return "Composite"; }
    void declare(PassBuilder& builder) override;
    void execute(PassContext& context, rhi::CommandList& commands) override;

    void setLetterboxColor(Color color) noexcept { m_letterbox = color; }

private:
    struct Pipeline {
        rhi::Format format = rhi::Format::Undefined;
        rhi::GraphicsPipelineHandle pipeline;
    };

    explicit CompositePass(rhi::Device& device) noexcept
        : m_device(device) {}

    // The pipeline into outputs of `format`, created on first use; null if creation failed.
    [[nodiscard]] rhi::GraphicsPipelineHandle pipeline(rhi::Format format);
    // The bind group that samples `texture`; recreated when the scene target is.
    [[nodiscard]] rhi::BindGroupHandle sceneGroup(rhi::TextureHandle texture);

    rhi::Device& m_device;
    rhi::ShaderModuleHandle m_vertexShader;
    rhi::ShaderModuleHandle m_fragmentShader;
    rhi::BindGroupLayoutHandle m_groupLayout;
    rhi::PipelineLayoutHandle m_pipelineLayout;
    rhi::SamplerHandle m_sampler;
    std::vector<Pipeline> m_pipelines;
    rhi::TextureHandle m_groupTexture;
    rhi::BindGroupHandle m_group;
    Color m_letterbox{};
};

} // namespace teserra::renderer
