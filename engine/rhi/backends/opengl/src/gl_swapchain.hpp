#pragma once

// The swapchain of the OpenGL backend (docs/architecture/rhi.md#opengl-backend-mapping). Render passes never
// target the default framebuffer: they render into an internal color texture, stored top row first like
// every RHI image (ADR-0024), and present() copies it to the default framebuffer with a vertical flip.

#include <teserra/core/math.hpp>
#include <teserra/rhi/handles.hpp>
#include <teserra/rhi/swapchain.hpp>

namespace teserra::platform {
class OpenGlContext;
}

namespace teserra::rhi::opengl {

class GlDevice;

class GlSwapchain final : public Swapchain {
public:
    /// Sets the swap interval of the present mode and creates the image for `framebufferSize`.
    GlSwapchain(GlDevice& device, platform::OpenGlContext& context, const SwapchainDesc& desc,
                UVec2 framebufferSize);
    ~GlSwapchain() override;

    GlSwapchain(const GlSwapchain&) = delete;
    GlSwapchain& operator=(const GlSwapchain&) = delete;
    GlSwapchain(GlSwapchain&&) = delete;
    GlSwapchain& operator=(GlSwapchain&&) = delete;

    [[nodiscard]] TextureHandle acquireNextImage() override { return m_image; }
    void present() override;
    void resize(UVec2 framebufferSize) override;
    [[nodiscard]] UVec2 size() const noexcept override { return m_size; }
    [[nodiscard]] Format format() const noexcept override { return m_format; }

private:
    void createImage();
    void releaseImage();

    GlDevice& m_device;
    platform::OpenGlContext& m_context;
    Format m_format;
    UVec2 m_size;
    TextureHandle m_image; // null while the size is 0x0
};

} // namespace teserra::rhi::opengl
