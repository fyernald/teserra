#pragma once

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/rhi/format.hpp>
#include <teserra/rhi/handles.hpp>

namespace teserra::rhi {

/// How presented images are paced.
enum class PresentMode : u8 {
    /// Waits for the display's vertical blank; never tears. Always supported.
    Fifo,
    /// Presents at once; may tear. Falls back to Fifo where unsupported.
    Immediate,
};

/// Creation parameters of a device's swapchain.
struct SwapchainDesc {
    /// Preferred format of the images. A backend may pick a close one (Vulkan surfaces offer a fixed set);
    /// Swapchain::format() tells which, and pipelines drawing into the swapchain declare that one.
    Format format = Format::Rgba8Srgb;
    PresentMode presentMode = PresentMode::Fifo;
};

/// The presentable images of a window. Owned by the Device that created it; main thread only.
class Swapchain {
public:
    Swapchain() = default;
    virtual ~Swapchain() = default;

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = delete;
    Swapchain& operator=(Swapchain&&) = delete;

    /// The image to render into this frame, as a color attachment. A null handle when there is none
    /// (minimized window, swapchain out of date): then skip rendering and presenting this frame.
    [[nodiscard]] virtual TextureHandle acquireNextImage() = 0;

    /// Shows the image acquired this frame, after the command lists rendering it were submitted.
    virtual void present() = 0;

    /// Recreates the images for a new framebuffer size (platform::WindowResized::framebufferSize).
    virtual void resize(UVec2 framebufferSize) = 0;

    /// Size of the images in pixels.
    [[nodiscard]] virtual UVec2 size() const noexcept = 0;

    /// Format of the images.
    [[nodiscard]] virtual Format format() const noexcept = 0;
};

} // namespace teserra::rhi
