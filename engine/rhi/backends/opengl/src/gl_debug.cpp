#include "gl_debug.hpp"

namespace teserra::rhi::opengl {

LogLevel debugMessageLevel(GLenum severity, GLenum type) noexcept {
    if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH) {
        return LogLevel::Error;
    }
    if (severity == GL_DEBUG_SEVERITY_MEDIUM || type == GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR ||
        type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR) {
        return LogLevel::Warn;
    }
    if (severity == GL_DEBUG_SEVERITY_LOW) {
        return LogLevel::Info;
    }
    return LogLevel::Debug;
}

std::string_view debugSourceName(GLenum source) noexcept {
    switch (source) {
        case GL_DEBUG_SOURCE_API:
            return "API";
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
            return "Window system";
        case GL_DEBUG_SOURCE_SHADER_COMPILER:
            return "Shader compiler";
        case GL_DEBUG_SOURCE_THIRD_PARTY:
            return "Third party";
        case GL_DEBUG_SOURCE_APPLICATION:
            return "Application";
        default:
            return "Other";
    }
}

std::string_view debugTypeName(GLenum type) noexcept {
    switch (type) {
        case GL_DEBUG_TYPE_ERROR:
            return "Error";
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
            return "Deprecated behavior";
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
            return "Undefined behavior";
        case GL_DEBUG_TYPE_PORTABILITY:
            return "Portability";
        case GL_DEBUG_TYPE_PERFORMANCE:
            return "Performance";
        case GL_DEBUG_TYPE_MARKER:
            return "Marker";
        case GL_DEBUG_TYPE_PUSH_GROUP:
            return "Push group";
        case GL_DEBUG_TYPE_POP_GROUP:
            return "Pop group";
        default:
            return "Other";
    }
}

void GLAD_API_PTR logDebugMessage(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                  const GLchar* message, const void* /*userParam*/) {
    std::string_view text =
        length >= 0 ? std::string_view(message, static_cast<usize>(length)) : std::string_view(message);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.remove_suffix(1);
    }

    const std::string_view typeName = debugTypeName(type);
    const std::string_view sourceName = debugSourceName(source);
    switch (debugMessageLevel(severity, type)) {
        case LogLevel::Error:
            TESERRA_LOG_ERROR(LogCategory::Rhi, "OpenGL {} {} from {}: {}", typeName, id, sourceName, text);
            break;
        case LogLevel::Warn:
            TESERRA_LOG_WARN(LogCategory::Rhi, "OpenGL {} {} from {}: {}", typeName, id, sourceName, text);
            break;
        case LogLevel::Info:
            TESERRA_LOG_INFO(LogCategory::Rhi, "OpenGL {} {} from {}: {}", typeName, id, sourceName, text);
            break;
        default:
            TESERRA_LOG_DEBUG(LogCategory::Rhi, "OpenGL {} {} from {}: {}", typeName, id, sourceName, text);
            break;
    }
}

} // namespace teserra::rhi::opengl
