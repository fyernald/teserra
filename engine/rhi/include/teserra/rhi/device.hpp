#pragma once

// The device: factory and owner of every GPU object, command submission and frame pacing
// (docs/architecture/rhi.md#objects). Devices are created through createDevice() with the backends the
// build includes; engine code only sees this interface.

#include <teserra/core/config.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/rhi/buffer.hpp>
#include <teserra/rhi/command_list.hpp>
#include <teserra/rhi/handles.hpp>
#include <teserra/rhi/limits.hpp>
#include <teserra/rhi/pipeline.hpp>
#include <teserra/rhi/sampler.hpp>
#include <teserra/rhi/shader.hpp>
#include <teserra/rhi/swapchain.hpp>
#include <teserra/rhi/texture.hpp>

#include <memory>
#include <span>
#include <string_view>

namespace teserra::platform {
class Window;
}

namespace teserra::rhi {

/// Graphics API behind a Device.
enum class Backend : u8 {
    OpenGL, ///< OpenGL 4.5 core (ADR-0004).
    Vulkan, ///< Vulkan 1.3 (future).
    Null,   ///< No GPU: records and validates, for headless tests.
};

/// Name of a backend, e.g. "OpenGL".
[[nodiscard]] std::string_view backendName(Backend backend) noexcept;

/// Creation parameters of a device.
struct DeviceDesc {
    Backend backend = Backend::OpenGL;
    /// RHI-level checks of descriptors and command lists before they reach the backend.
    bool enableValidation = TESERRA_DEBUG_BUILD != 0;
    /// The backend's own debug facilities: driver debug output, validation layers.
    bool enableBackendDebug = TESERRA_DEBUG_BUILD != 0;
    /// Used when the device is created for a window.
    SwapchainDesc swapchain{};
};

/// Factory and owner of GPU objects. Objects are referred to by handles; destroy() makes a handle stale
/// at once, while the backend keeps the GPU object until no frame in flight uses it. Created, used and
/// destroyed on the main thread. Virtual calls happen per object and per submission, never per draw:
/// draws are recorded into a CommandList.
class Device {
public:
    Device() = default;
    virtual ~Device() = default;

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

    [[nodiscard]] virtual Backend backend() const noexcept = 0;
    [[nodiscard]] virtual const DeviceLimits& limits() const noexcept = 0;

    [[nodiscard]] virtual Result<BufferHandle> createBuffer(const BufferDesc& desc) = 0;
    [[nodiscard]] virtual Result<TextureHandle> createTexture(const TextureDesc& desc) = 0;
    [[nodiscard]] virtual Result<SamplerHandle> createSampler(const SamplerDesc& desc) = 0;
    /// Fails with ErrorCode::CompilationFailed if the backend cannot compile the shader.
    [[nodiscard]] virtual Result<ShaderModuleHandle> createShaderModule(const ShaderModuleDesc& desc) = 0;
    [[nodiscard]] virtual Result<GraphicsPipelineHandle>
    createGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;

    /// Destroys the object behind a handle; null handles are ignored. Using a destroyed handle is a bug
    /// that validation reports.
    virtual void destroy(BufferHandle handle) = 0;
    virtual void destroy(TextureHandle handle) = 0;
    virtual void destroy(SamplerHandle handle) = 0;
    virtual void destroy(ShaderModuleHandle handle) = 0;
    virtual void destroy(GraphicsPipelineHandle handle) = 0;

    /// Starts a frame: waits until the frame slot being reused is no longer in flight, then releases what
    /// it held (command lists, destroyed objects).
    virtual void beginFrame() = 0;
    /// Ends the frame started by beginFrame().
    virtual void endFrame() = 0;

    /// An empty command list owned by the current frame, valid until the frame slot is reused.
    [[nodiscard]] virtual CommandList& acquireCommandList() = 0;
    /// Hands recorded commands to the GPU, in submission order.
    virtual void submit(CommandList& commands) = 0;

    /// The swapchain of the device's window; null for a headless device.
    [[nodiscard]] virtual Swapchain* swapchain() noexcept = 0;

    /// Waits until the GPU has finished all submitted work: for shutdown, resizing and tests.
    virtual void waitIdle() = 0;
};

/// A backend's device constructor. `window` is null for a headless device; a backend that needs a window
/// fails with ErrorCode::Unsupported then.
using CreateDeviceFunction = Result<std::unique_ptr<Device>> (*)(const DeviceDesc& desc,
                                                                 platform::Window* window);

/// How a backend is offered to createDevice(). Each backend library exports one (for example
/// rhi::null::backendFactory()); the engine passes those the build includes.
struct BackendFactory {
    Backend backend = Backend::Null;
    CreateDeviceFunction create = nullptr;
};

/// Creates a device with the factory for `desc.backend` among `backends`, for `window` (null for a
/// headless device). Fails with ErrorCode::Unsupported if no factory is given for that backend, i.e. it is
/// not compiled in. Each backend may appear at most once in `backends`.
[[nodiscard]] Result<std::unique_ptr<Device>> createDevice(const DeviceDesc& desc, platform::Window* window,
                                                           std::span<const BackendFactory> backends);

} // namespace teserra::rhi
