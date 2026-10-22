#pragma once

// The OpenGL 4.5 Device (docs/architecture/rhi.md#opengl-backend-mapping). GL functions are called through
// the device's own glad function table (MX mode, ADR-0009), on the thread that created the device, with the
// window's context current.

#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/rhi/command_list.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/limits.hpp>

#include <glad/gl.h>

#include <array>
#include <deque>
#include <memory>
#include <thread>

namespace teserra::platform {
class OpenGlContext;
class Window;
} // namespace teserra::platform

namespace teserra::rhi::opengl {

class GlDevice final : public Device {
public:
    /// The BackendFactory::create of the OpenGL backend.
    [[nodiscard]] static Result<std::unique_ptr<Device>> create(const DeviceDesc& desc,
                                                                platform::Window* window);

    explicit GlDevice(platform::OpenGlContext& context);

    [[nodiscard]] Backend backend() const noexcept override { return Backend::OpenGL; }
    [[nodiscard]] const DeviceLimits& limits() const noexcept override { return m_limits; }

    [[nodiscard]] Result<BufferHandle> createBuffer(const BufferDesc& desc) override;
    [[nodiscard]] Result<TextureHandle> createTexture(const TextureDesc& desc) override;
    [[nodiscard]] Result<SamplerHandle> createSampler(const SamplerDesc& desc) override;
    [[nodiscard]] Result<ShaderModuleHandle> createShaderModule(const ShaderModuleDesc& desc) override;
    [[nodiscard]] Result<GraphicsPipelineHandle>
    createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;

    void destroy(BufferHandle handle) override;
    void destroy(TextureHandle handle) override;
    void destroy(SamplerHandle handle) override;
    void destroy(ShaderModuleHandle handle) override;
    void destroy(GraphicsPipelineHandle handle) override;

    void beginFrame() override;
    void endFrame() override;

    [[nodiscard]] CommandList& acquireCommandList() override;
    void submit(CommandList& commands) override;

    [[nodiscard]] Swapchain* swapchain() noexcept override;

    void waitIdle() override;

private:
    // Loads GL, checks the version and limits, and sets up debug output and clip conventions.
    [[nodiscard]] Result<void> initialize(const DeviceDesc& desc);
    [[nodiscard]] Result<void> queryLimits();
    [[nodiscard]] i32 getInteger(GLenum name) const;
    void requireContextThread() const;

    platform::OpenGlContext& m_context;
    GladGLContext m_gl{};
    DeviceLimits m_limits{};
    std::thread::id m_thread;

    u64 m_frame = 0;
    bool m_inFrame = false;
    // Command lists of each frame slot; a deque keeps handed-out references valid when it grows.
    std::array<std::deque<CommandList>, kMaxFramesInFlight> m_commandLists;
    usize m_commandListsInUse = 0; // of the current frame slot
};

} // namespace teserra::rhi::opengl
