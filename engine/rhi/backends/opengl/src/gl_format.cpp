#include "gl_format.hpp"

#include <teserra/core/assert.hpp>

namespace teserra::rhi::opengl {

GLenum glInternalFormat(Format format) {
    switch (format) {
        case Format::R8Unorm:
            return GL_R8;
        case Format::Rg8Unorm:
            return GL_RG8;
        case Format::Rgba8Unorm:
        case Format::Bgra8Unorm:
            return GL_RGBA8;
        case Format::Rgba8Srgb:
        case Format::Bgra8Srgb:
            return GL_SRGB8_ALPHA8;
        case Format::Rgb10A2Unorm:
            return GL_RGB10_A2;
        case Format::R16Float:
            return GL_R16F;
        case Format::Rg16Float:
            return GL_RG16F;
        case Format::Rgba16Float:
            return GL_RGBA16F;
        case Format::R32Float:
            return GL_R32F;
        case Format::Rg32Float:
            return GL_RG32F;
        case Format::Rgba32Float:
            return GL_RGBA32F;
        case Format::R32Uint:
            return GL_R32UI;
        case Format::Depth16Unorm:
            return GL_DEPTH_COMPONENT16;
        case Format::Depth24UnormStencil8:
            return GL_DEPTH24_STENCIL8;
        case Format::Depth32Float:
            return GL_DEPTH_COMPONENT32F;
        case Format::Depth32FloatStencil8:
            return GL_DEPTH32F_STENCIL8;
        case Format::Undefined:
            break;
    }
    TESERRA_UNREACHABLE("No OpenGL internal format for {}", formatInfo(format).name);
}

Format swapchainFormat(Format preferred) noexcept {
    switch (preferred) {
        case Format::Rgba8Srgb:
        case Format::Rgba8Unorm:
        case Format::Rgb10A2Unorm:
        case Format::Rgba16Float:
            return preferred;
        case Format::Bgra8Srgb:
            return Format::Rgba8Srgb;
        case Format::Bgra8Unorm:
            return Format::Rgba8Unorm;
        default:
            return Format::Rgba8Srgb;
    }
}

GlVertexFormat glVertexFormat(VertexFormat format) noexcept {
    switch (format) {
        case VertexFormat::Float:
            return {.components = 1, .type = GL_FLOAT};
        case VertexFormat::Float2:
            return {.components = 2, .type = GL_FLOAT};
        case VertexFormat::Float3:
            return {.components = 3, .type = GL_FLOAT};
        case VertexFormat::Float4:
            return {.components = 4, .type = GL_FLOAT};
        case VertexFormat::Unorm8x4:
            return {.components = 4, .type = GL_UNSIGNED_BYTE, .normalized = true};
        case VertexFormat::Uint8x4:
            return {.components = 4, .type = GL_UNSIGNED_BYTE, .integer = true};
        case VertexFormat::Uint16x2:
            return {.components = 2, .type = GL_UNSIGNED_SHORT, .integer = true};
        case VertexFormat::Uint32:
            return {.components = 1, .type = GL_UNSIGNED_INT, .integer = true};
    }
    return {};
}

} // namespace teserra::rhi::opengl
