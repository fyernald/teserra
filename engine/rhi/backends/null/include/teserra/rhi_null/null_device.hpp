#pragma once

// The null backend (docs/architecture/rhi.md#null-backend): a Device without a GPU, for tests and headless
// runs. It keeps resources in handle pools, validates descriptors and submitted command lists, and keeps the
// lists of the last frame for tests to inspect. Nothing is drawn.
//
//   auto device = rhi::null::createHeadlessDevice();
//   device->beginFrame();
//   rhi::CommandList& commands = device->acquireCommandList();
//   ...
//   device->submit(commands);
//   device->endFrame();
//   CHECK(rhi::describeCommands(*device->submitted()[0]) == "...");

#include <teserra/core/handle_pool.hpp>
#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/rhi/command_list.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/limits.hpp>

#include <array>
#include <deque>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace teserra::rhi::null {

class NullDevice;

/// The null backend, for createDevice(). With a window, the device has a swapchain of the window's
/// framebuffer size; without one, it has none. Creation never fails.
[[nodiscard]] BackendFactory backendFactory() noexcept;

/// A null device without a window, for tests: `desc.backend` is ignored.
[[nodiscard]] std::unique_ptr<NullDevice> createHeadlessDevice(const DeviceDesc& desc = {});

/// The swapchain of a null device: one image, a texture of the device with RenderTarget usage.
class NullSwapchain final : public Swapchain {
public:
    NullSwapchain(NullDevice& device, const SwapchainDesc& desc, UVec2 framebufferSize);
    ~NullSwapchain() override;

    NullSwapchain(const NullSwapchain&) = delete;
    NullSwapchain& operator=(const NullSwapchain&) = delete;
    NullSwapchain(NullSwapchain&&) = delete;
    NullSwapchain& operator=(NullSwapchain&&) = delete;

    [[nodiscard]] TextureHandle acquireNextImage() override { return m_image; }
    void present() override;
    /// A new size gets a new image handle; 0x0 (minimized) gets none until the next resize.
    void resize(UVec2 framebufferSize) override;
    [[nodiscard]] UVec2 size() const noexcept override { return m_size; }
    [[nodiscard]] Format format() const noexcept override { return m_format; }

    /// Calls of present() so far.
    [[nodiscard]] u64 presentCount() const noexcept { return m_presentCount; }

private:
    void createImage();
    void releaseImage();

    NullDevice& m_device;
    Format m_format;
    UVec2 m_size;
    TextureHandle m_image;
    u64 m_presentCount = 0;
};

class NullDevice final : public Device {
public:
    /// `window` may be null for a headless device.
    NullDevice(const DeviceDesc& desc, platform::Window* window);
    ~NullDevice() override;

    NullDevice(const NullDevice&) = delete;
    NullDevice& operator=(const NullDevice&) = delete;
    NullDevice(NullDevice&&) = delete;
    NullDevice& operator=(NullDevice&&) = delete;

    [[nodiscard]] Backend backend() const noexcept override { return Backend::Null; }
    /// The guaranteed minimums of DeviceLimits.
    [[nodiscard]] const DeviceLimits& limits() const noexcept override { return m_limits; }

    [[nodiscard]] Result<BufferHandle> createBuffer(const BufferDesc& desc) override;
    [[nodiscard]] Result<TextureHandle> createTexture(const TextureDesc& desc) override;
    [[nodiscard]] Result<SamplerHandle> createSampler(const SamplerDesc& desc) override;
    [[nodiscard]] Result<ShaderModuleHandle> createShaderModule(const ShaderModuleDesc& desc) override;
    /// Asserts that the shaders are alive.
    [[nodiscard]] Result<GraphicsPipelineHandle>
    createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;

    /// Frees the object at once (there is no GPU to wait for). Null handles are ignored; stale ones assert.
    void destroy(BufferHandle handle) override;
    void destroy(TextureHandle handle) override;
    void destroy(SamplerHandle handle) override;
    void destroy(ShaderModuleHandle handle) override;
    void destroy(GraphicsPipelineHandle handle) override;

    /// Copies into the buffer's CPU memory (bufferData()).
    void writeBuffer(BufferHandle buffer, u64 offset, std::span<const std::byte> data) override;
    /// The buffer's CPU memory. Copy commands are validated but not executed, so the GPU never writes it.
    [[nodiscard]] std::span<std::byte> map(BufferHandle buffer) override;

    /// Starts a frame and forgets the lists submitted in the previous one (submitted()).
    void beginFrame() override;
    void endFrame() override;

    [[nodiscard]] CommandList& acquireCommandList() override;
    /// Finishes the list, validates it against the device's objects when DeviceDesc::enableValidation is
    /// set (handle liveness, usages, pipeline and render pass formats, copy ranges) and keeps it.
    void submit(CommandList& commands) override;

    [[nodiscard]] Swapchain* swapchain() noexcept override { return m_swapchain.get(); }

    void waitIdle() override {}

    // Inspection, for tests.

    /// The lists submitted since the last beginFrame(), in order. They stay valid until their frame slot is
    /// reused, kMaxFramesInFlight frames later.
    [[nodiscard]] std::span<const CommandList* const> submitted() const noexcept { return m_submitted; }
    /// Frames ended so far.
    [[nodiscard]] u64 frameCount() const noexcept { return m_frame; }
    /// Live objects of every kind, swapchain images included.
    [[nodiscard]] usize liveObjectCount() const noexcept;

    /// The descriptor an object was created with, or nullptr if the handle is not alive. Its views (debug
    /// name, shader code, vertex layouts) point into copies the device owns, valid while the object lives.
    [[nodiscard]] const BufferDesc* buffer(BufferHandle handle) const noexcept;
    [[nodiscard]] const TextureDesc* texture(TextureHandle handle) const noexcept;
    [[nodiscard]] const SamplerDesc* sampler(SamplerHandle handle) const noexcept;
    [[nodiscard]] const ShaderModuleDesc* shaderModule(ShaderModuleHandle handle) const noexcept;
    [[nodiscard]] const GraphicsPipelineDesc* graphicsPipeline(GraphicsPipelineHandle handle) const noexcept;
    /// The memory of a buffer, zero-initialized at creation and written by writeBuffer() and through map();
    /// empty if the handle is not alive.
    [[nodiscard]] std::span<const std::byte> bufferData(BufferHandle handle) const noexcept;

private:
    friend class NullSwapchain;

    // Each object owns the data its descriptor's views point to; behind a unique_ptr so that it does not
    // move when a pool grows.
    template <typename Desc>
    struct Stored {
        Desc desc;
        std::string debugName;
        std::string entryPoint;
        std::vector<std::byte> blob;
        std::vector<std::byte> data; // a buffer's memory
        std::vector<VertexBufferLayout> vertexBuffers;
        std::vector<VertexAttribute> vertexAttributes;
    };
    template <typename Desc>
    using Object = std::unique_ptr<Stored<Desc>>;

    template <typename Desc>
    [[nodiscard]] static Object<Desc> store(const Desc& desc);

    [[nodiscard]] TextureHandle createSwapchainImage(Format format, UVec2 size);
    void validate(const CommandList& commands) const;

    DeviceLimits m_limits{};
    bool m_validate;
    HandlePool<Object<BufferDesc>, BufferTag> m_buffers;
    HandlePool<Object<TextureDesc>, TextureTag> m_textures;
    HandlePool<Object<SamplerDesc>, SamplerTag> m_samplers;
    HandlePool<Object<ShaderModuleDesc>, ShaderModuleTag> m_shaderModules;
    HandlePool<Object<GraphicsPipelineDesc>, GraphicsPipelineTag> m_pipelines;
    std::unique_ptr<NullSwapchain> m_swapchain;

    u64 m_frame = 0;
    bool m_inFrame = false;
    // Command lists of each frame slot; a deque keeps handed-out references valid when it grows.
    std::array<std::deque<CommandList>, kMaxFramesInFlight> m_commandLists;
    usize m_commandListsInUse = 0; // of the current frame slot
    std::vector<const CommandList*> m_submitted;
};

} // namespace teserra::rhi::null
