#pragma once

// The OpenGL 4.5 Device (docs/architecture/rhi.md#opengl-backend-mapping). GL functions are called through
// the device's own glad function table (MX mode, ADR-0009), on the thread that created the device, with the
// window's context current.

#include <teserra/core/handle_pool.hpp>
#include <teserra/core/math.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/rhi/command_list.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/limits.hpp>

#include <glad/gl.h>

#include <array>
#include <cstddef>
#include <deque>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace teserra::platform {
class OpenGlContext;
class Window;
} // namespace teserra::platform

namespace teserra::rhi::opengl {

class GlSwapchain;

/// A buffer of the device.
struct GlBuffer {
    GLuint name = 0;
    u64 size = 0;
    BufferUsage usage = BufferUsage::None;
    MemoryLocation memory = MemoryLocation::GpuOnly;
    /// The persistent mapping of an Upload or Readback buffer; null for GpuOnly.
    std::byte* mapped = nullptr;
    std::string debugName;

    /// The descriptor the buffer was created with, for validation; its name is valid while the buffer lives.
    [[nodiscard]] BufferDesc desc() const noexcept {
        return {.sizeInBytes = size, .usage = usage, .memory = memory, .debugName = debugName};
    }
};

/// A texture of the device.
struct GlTexture {
    GLuint name = 0;
    Format format = Format::Undefined;
    UVec2 size{};
};

/// The attachments a framebuffer object is made of; framebuffers are cached by it.
struct FramebufferKey {
    struct Attachment {
        TextureHandle texture{};
        u32 mipLevel = 0;
        u32 arrayLayer = 0;

        friend bool operator==(const Attachment&, const Attachment&) = default;
    };

    std::array<Attachment, kMaxColorAttachments> color{};

    friend bool operator==(const FramebufferKey&, const FramebufferKey&) = default;
};

class GlDevice final : public Device {
public:
    /// The BackendFactory::create of the OpenGL backend.
    [[nodiscard]] static Result<std::unique_ptr<Device>> create(const DeviceDesc& desc,
                                                                platform::Window* window);

    explicit GlDevice(platform::OpenGlContext& context);
    /// Deletes the device's GL objects; the window's context must still exist.
    ~GlDevice() override;

    GlDevice(const GlDevice&) = delete;
    GlDevice& operator=(const GlDevice&) = delete;
    GlDevice(GlDevice&&) = delete;
    GlDevice& operator=(GlDevice&&) = delete;

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

    void writeBuffer(BufferHandle buffer, u64 offset, std::span<const std::byte> data) override;
    [[nodiscard]] std::span<std::byte> map(BufferHandle buffer) override;

    void beginFrame() override;
    void endFrame() override;

    [[nodiscard]] CommandList& acquireCommandList() override;
    void submit(CommandList& commands) override;

    [[nodiscard]] Swapchain* swapchain() noexcept override;

    void waitIdle() override;

    // For the swapchain and tests within the backend.

    [[nodiscard]] const GladGLContext& gl() const noexcept { return m_gl; }
    /// Creates a 2D texture with one mip level, labeled `label` for graphics debuggers.
    [[nodiscard]] TextureHandle createTexture2D(Format format, UVec2 size, std::string_view label);
    /// Deletes a texture and the framebuffers that use it.
    void releaseTexture(TextureHandle handle);
    /// The texture behind `handle`, or nullptr if the handle is not live.
    [[nodiscard]] const GlTexture* texture(TextureHandle handle) const noexcept {
        return m_textures.tryGet(handle);
    }
    /// The framebuffer object with `texture` as its only color attachment.
    [[nodiscard]] GLuint colorFramebuffer(TextureHandle texture);

private:
    struct CachedFramebuffer {
        FramebufferKey key;
        GLuint name = 0;
    };

    // Loads GL, checks the version and limits, and sets up debug output, conventions and the swapchain.
    [[nodiscard]] Result<void> initialize(const DeviceDesc& desc, platform::Window& window);
    [[nodiscard]] Result<void> queryLimits();
    [[nodiscard]] i32 getInteger(GLenum name) const;
    void requireContextThread() const;

    [[nodiscard]] GLuint framebuffer(const FramebufferKey& key);
    // Replays the commands of a list (docs/architecture/rhi.md#opengl-backend-mapping).
    void replay(const CommandList& commands);
    void copyBuffer(const BufferCopy& copy);
    void beginRenderPass(const RenderPassDesc& pass);
    void endRenderPass();

    platform::OpenGlContext& m_context;
    GladGLContext m_gl{};
    DeviceLimits m_limits{};
    bool m_validate = false;
    std::thread::id m_thread;

    HandlePool<GlBuffer, BufferTag> m_buffers;
    HandlePool<GlTexture, TextureTag> m_textures;
    std::vector<CachedFramebuffer> m_framebuffers;
    std::unique_ptr<GlSwapchain> m_swapchain;

    // Replay state: the open render pass's framebuffer and its attachments that are discarded at its end.
    GLuint m_passFramebuffer = 0;
    std::array<GLenum, kMaxColorAttachments> m_passDiscards{};
    u32 m_passDiscardCount = 0;

    u64 m_frame = 0;
    bool m_inFrame = false;
    // Command lists of each frame slot; a deque keeps handed-out references valid when it grows.
    std::array<std::deque<CommandList>, kMaxFramesInFlight> m_commandLists;
    usize m_commandListsInUse = 0; // of the current frame slot
};

} // namespace teserra::rhi::opengl
