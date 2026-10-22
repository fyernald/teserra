#include "gl_swapchain.hpp"

#include <teserra/core/assert.hpp>
#include <teserra/core/log.hpp>
#include <teserra/platform/opengl_context.hpp>

#include "gl_device.hpp"
#include "gl_format.hpp"

namespace teserra::rhi::opengl {

GlSwapchain::GlSwapchain(GlDevice& device, platform::OpenGlContext& context, const SwapchainDesc& desc,
                         UVec2 framebufferSize)
    : m_device(device), m_context(context), m_format(swapchainFormat(desc.format)), m_size(framebufferSize) {
    if (m_format != desc.format) {
        TESERRA_LOG_INFO(LogCategory::Rhi, "Swapchain format {} instead of the preferred {}",
                         formatInfo(m_format).name, formatInfo(desc.format).name);
    }
    // Fifo waits for the vertical blank; Immediate does not.
    m_context.setSwapInterval(desc.presentMode == PresentMode::Immediate ? 0 : 1);
    createImage();
}

GlSwapchain::~GlSwapchain() {
    releaseImage();
}

void GlSwapchain::present() {
    TESERRA_ASSERT(!m_image.isNull(), "present() without an image; skip presenting while acquireNextImage() "
                                      "returns a null handle");
    if (m_image.isNull()) {
        return;
    }
    const GladGLContext& gl = m_device.gl();
    const auto width = static_cast<GLint>(m_size.x);
    const auto height = static_cast<GLint>(m_size.y);
    // The image is stored top row first; the default framebuffer shows its row 0 at the bottom. With sRGB
    // conversion off, the blit copies the encoded values unchanged.
    gl.Disable(GL_FRAMEBUFFER_SRGB);
    gl.Disable(GL_SCISSOR_TEST);
    gl.BlitNamedFramebuffer(m_device.colorFramebuffer(m_image), 0, 0, 0, width, height, 0, height, width, 0,
                            GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl.Enable(GL_FRAMEBUFFER_SRGB);
    m_context.swapBuffers();
}

void GlSwapchain::resize(UVec2 framebufferSize) {
    if (framebufferSize == m_size) {
        return;
    }
    // A new handle for every size: framebuffers cached for the old image go with it, and stale uses of it
    // are detected.
    releaseImage();
    m_size = framebufferSize;
    createImage();
}

void GlSwapchain::createImage() {
    if (m_size.x == 0 || m_size.y == 0) {
        return; // minimized: no image until the next resize
    }
    m_image = m_device.createTexture2D(m_format, m_size, TextureUsage::RenderTarget | TextureUsage::CopySrc,
                                       "Swapchain image");
}

void GlSwapchain::releaseImage() {
    if (!m_image.isNull()) {
        m_device.releaseTexture(m_image);
        m_image = {};
    }
}

} // namespace teserra::rhi::opengl
