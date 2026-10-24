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
#include <teserra/rhi/shader_blob.hpp>
#include <teserra/rhi/validation.hpp>

#include "gl_pipeline_state.hpp"
#include <glad/gl.h>

#include <array>
#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
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

    /// The descriptor the buffer was created with, for validation; its name is valid until the device creates
    /// or destroys objects.
    [[nodiscard]] BufferDesc desc() const noexcept {
        return {.sizeInBytes = size, .usage = usage, .memory = memory, .debugName = debugName};
    }
};

/// A compiled shader stage; pipelines link programs from these (#34).
struct GlShaderModule {
    GLuint shader = 0;
    ShaderStage stage = ShaderStage::Vertex;
    // Of the .tshader blob, for validation.
    ShaderReflection reflection;
    std::string debugName;
};

/// A graphics pipeline: a program shared by the pipelines with the same shaders, its own vertex array, and
/// the state that binding it sets.
struct GlPipeline {
    GLuint program = 0;
    GLuint vertexArray = 0;
    GlPipelineState state{};
    GLenum primitiveMode = GL_TRIANGLES;
    /// The vertex buffer slots; draws bind buffers with their strides.
    std::array<VertexBufferLayout, kMaxVertexBuffers> vertexBuffers{};
    u32 vertexBufferCount = 0;
    /// The descriptor without its views (vertex layouts, attributes, name), for validation; see desc().
    GraphicsPipelineDesc stored{};
    // The groups of the layout the pipeline was created with, for validation.
    StoredPipelineLayout layout;
    std::string debugName;

    /// The descriptor the pipeline was created with, with its vertex buffer layouts but not its attributes.
    /// Its views are valid until the device creates or destroys objects.
    [[nodiscard]] GraphicsPipelineDesc desc() const noexcept {
        GraphicsPipelineDesc result = stored;
        result.vertexBuffers = std::span(vertexBuffers.data(), vertexBufferCount);
        result.debugName = debugName;
        return result;
    }
};

/// A texture of the device.
struct GlTexture {
    GLuint name = 0;
    Format format = Format::Undefined;
    UVec2 size{};
    TextureUsage usage = TextureUsage::None;
    std::string debugName;

    /// The descriptor the texture was created with, for validation; its name is valid until the device
    /// creates or destroys objects.
    [[nodiscard]] TextureDesc desc() const noexcept {
        return {.format = format, .width = size.x, .height = size.y, .usage = usage, .debugName = debugName};
    }
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
    /// Bind group layouts, pipeline layouts and bind groups keep their descriptors; GL has no such objects.
    /// Layouts with storage buffers are Unsupported (#190).
    [[nodiscard]] Result<BindGroupLayoutHandle>
    createBindGroupLayout(const BindGroupLayoutDesc& desc) override;
    [[nodiscard]] Result<PipelineLayoutHandle> createPipelineLayout(const PipelineLayoutDesc& desc) override;
    [[nodiscard]] Result<BindGroupHandle> createBindGroup(const BindGroupDesc& desc) override;
    [[nodiscard]] Result<GraphicsPipelineHandle>
    createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;

    void destroy(BufferHandle handle) override;
    void destroy(TextureHandle handle) override;
    void destroy(SamplerHandle handle) override;
    void destroy(ShaderModuleHandle handle) override;
    void destroy(BindGroupLayoutHandle handle) override;
    void destroy(PipelineLayoutHandle handle) override;
    void destroy(BindGroupHandle handle) override;
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
    /// Creates a 2D texture with one mip level, named `debugName` for graphics debuggers and validation.
    [[nodiscard]] TextureHandle createTexture2D(Format format, UVec2 size, TextureUsage usage,
                                                std::string_view debugName);
    /// Deletes a texture and the framebuffers that use it.
    void releaseTexture(TextureHandle handle);
    /// The texture behind `handle`, or nullptr if the handle is not live.
    [[nodiscard]] const GlTexture* texture(TextureHandle handle) const noexcept {
        return m_textures.tryGet(handle);
    }
    /// The shader module behind `handle`, or nullptr if the handle is not live.
    [[nodiscard]] const GlShaderModule* shaderModule(ShaderModuleHandle handle) const noexcept {
        return m_shaderModules.tryGet(handle);
    }
    /// The pipeline behind `handle`, or nullptr if the handle is not live.
    [[nodiscard]] const GlPipeline* pipeline(GraphicsPipelineHandle handle) const noexcept {
        return m_pipelines.tryGet(handle);
    }
    /// The buffer behind `handle`, or nullptr if the handle is not live.
    [[nodiscard]] const GlBuffer* buffer(BufferHandle handle) const noexcept {
        return m_buffers.tryGet(handle);
    }
    /// The bind group layout behind `handle`, or nullptr if the handle is not live.
    [[nodiscard]] const StoredBindGroupLayout* bindGroupLayout(BindGroupLayoutHandle handle) const noexcept {
        return m_bindGroupLayouts.tryGet(handle);
    }
    /// The pipeline layout behind `handle`, or nullptr if the handle is not live.
    [[nodiscard]] const StoredPipelineLayout* pipelineLayout(PipelineLayoutHandle handle) const noexcept {
        return m_pipelineLayouts.tryGet(handle);
    }
    /// The bind group behind `handle`, or nullptr if the handle is not live.
    [[nodiscard]] const StoredBindGroup* bindGroup(BindGroupHandle handle) const noexcept {
        return m_bindGroups.tryGet(handle);
    }
    /// The framebuffer object with `texture` as its only color attachment.
    [[nodiscard]] GLuint colorFramebuffer(TextureHandle texture);

private:
    struct CachedFramebuffer {
        FramebufferKey key;
        GLuint name = 0;
    };
    // A linked program and the number of pipelines that use it.
    struct CachedProgram {
        ShaderModuleHandle vertexShader{};
        ShaderModuleHandle fragmentShader{};
        GLuint program = 0;
        u32 users = 0;
    };

    // Loads GL, checks the version and limits, and sets up debug output, conventions and the swapchain.
    [[nodiscard]] Result<void> initialize(const DeviceDesc& desc, platform::Window& window);
    [[nodiscard]] Result<void> queryLimits();
    [[nodiscard]] i32 getInteger(GLenum name) const;
    void requireContextThread() const;

    [[nodiscard]] GLuint framebuffer(const FramebufferKey& key);
    // The program of a shader pair, linked on first use; the caller becomes one of its users.
    [[nodiscard]] Result<GLuint> acquireProgram(const GraphicsPipelineDesc& desc,
                                                const GlShaderModule& vertex, const GlShaderModule* fragment);
    void releaseProgram(GLuint program);
    // Replays the commands of a list (docs/architecture/rhi.md#opengl-backend-mapping).
    void replay(const CommandList& commands);
    void copyBuffer(const BufferCopy& copy);
    void bindPipeline(GraphicsPipelineHandle handle);
    // Binds the group's buffers at flatBinding(set, binding), with the dynamic offsets applied.
    void applyBindGroup(u32 set, BindGroupHandle handle, std::span<const u32> dynamicOffsets);
    void bindVertexBuffers(u32 firstSlot, std::span<const VertexBufferBinding> bindings);
    void bindIndexBuffer(const BindIndexBufferCommand& bind);
    // Applies the pass's buffer bindings to the bound pipeline's vertex array if they or the vertex array
    // changed since the last draw. False if there is no pipeline to draw with.
    [[nodiscard]] bool flushVertexInput();
    void draw(const DrawArgs& args);
    void drawIndexed(const DrawIndexedArgs& args);
    void beginRenderPass(const RenderPassDesc& pass);
    void endRenderPass();

    platform::OpenGlContext& m_context;
    GladGLContext m_gl{};
    DeviceLimits m_limits{};
    bool m_validate = false;
    std::thread::id m_thread;

    HandlePool<GlBuffer, BufferTag> m_buffers;
    HandlePool<GlShaderModule, ShaderModuleTag> m_shaderModules;
    HandlePool<StoredBindGroupLayout, BindGroupLayoutTag> m_bindGroupLayouts;
    HandlePool<StoredPipelineLayout, PipelineLayoutTag> m_pipelineLayouts;
    HandlePool<StoredBindGroup, BindGroupTag> m_bindGroups;
    HandlePool<GlPipeline, GraphicsPipelineTag> m_pipelines;
    std::vector<CachedProgram> m_programs;
    GlStateCache m_stateCache{m_gl};
    HandlePool<GlTexture, TextureTag> m_textures;
    std::vector<CachedFramebuffer> m_framebuffers;
    std::unique_ptr<GlSwapchain> m_swapchain;

    // Replay state: the open render pass's framebuffer and its attachments that are discarded at its end.
    GLuint m_passFramebuffer = 0;
    std::array<GLenum, kMaxColorAttachments> m_passDiscards{};
    u32 m_passDiscardCount = 0;
    // The pass's bindings. Vertex and index buffers are vertex array state in GL, and each pipeline has its
    // own vertex array, so they are applied to the bound pipeline's vertex array at the next draw.
    struct BoundBuffer {
        GLuint name = 0;
        u64 offset = 0;
    };
    const GlPipeline* m_passPipeline = nullptr;
    std::array<BoundBuffer, kMaxVertexBuffers> m_passVertexBuffers{};
    BoundBuffer m_passIndexBuffer{};
    IndexType m_passIndexType = IndexType::Uint16;
    bool m_vertexInputDirty = true;
    GLuint m_flushedVertexArray = 0;

    u64 m_frame = 0;
    bool m_inFrame = false;
    // Command lists of each frame slot; a deque keeps handed-out references valid when it grows.
    std::array<std::deque<CommandList>, kMaxFramesInFlight> m_commandLists;
    usize m_commandListsInUse = 0; // of the current frame slot
};

} // namespace teserra::rhi::opengl
