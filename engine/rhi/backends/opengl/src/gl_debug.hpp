#pragma once

// KHR_debug output routed to the engine log (category Rhi), with GL severities mapped to log levels.

#include <teserra/core/log.hpp>

#include <glad/gl.h>

#include <string_view>

namespace teserra::rhi::opengl {

/// Log level of a debug message: Error for errors and high severity; Warn for medium severity, deprecated
/// and undefined behavior; Info for low severity; Debug for notifications, which drivers send in numbers.
[[nodiscard]] LogLevel debugMessageLevel(GLenum severity, GLenum type) noexcept;

/// "API", "Shader compiler", ... for a GL_DEBUG_SOURCE_* value.
[[nodiscard]] std::string_view debugSourceName(GLenum source) noexcept;

/// "Error", "Performance", ... for a GL_DEBUG_TYPE_* value.
[[nodiscard]] std::string_view debugTypeName(GLenum type) noexcept;

/// The callback installed with glDebugMessageCallback: logs "OpenGL <type> <id> from <source>: <message>"
/// at debugMessageLevel().
void GLAD_API_PTR logDebugMessage(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                  const GLchar* message, const void* userParam);

} // namespace teserra::rhi::opengl
