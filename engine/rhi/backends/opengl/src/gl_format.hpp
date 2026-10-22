#pragma once

// RHI formats as OpenGL sees them.

#include <teserra/rhi/format.hpp>

#include <glad/gl.h>

namespace teserra::rhi::opengl {

/// The sized internal format for textures of `format`, e.g. GL_SRGB8_ALPHA8 for Rgba8Srgb. BGRA formats
/// share the RGBA internal format: GL has no BGRA storage, only BGRA uploads. Asserts on Undefined.
[[nodiscard]] GLenum glInternalFormat(Format format);

/// The format the swapchain uses when `preferred` is asked for: the same where the swapchain supports it,
/// the RGBA format of the same encoding for BGRA, and Rgba8Srgb for anything else.
[[nodiscard]] Format swapchainFormat(Format preferred) noexcept;

} // namespace teserra::rhi::opengl
