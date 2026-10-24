#pragma once

// A fake OpenGL driver for tests without a GPU. FakeGlContext::getProcAddress() returns the fake functions
// below, which, like a real driver, act on the context made current last on the calling thread: they report
// its version, strings, extensions and limits, hand out object names, keep buffer contents, and record what
// the device does as a log of calls ("ClearNamedFramebufferfv(1, GL_COLOR, 0, 0.1 0.2 0.3 1)") that tests
// compare.

#include <teserra/core/math.hpp>
#include <teserra/platform/opengl_context.hpp>
#include <teserra/platform/window.hpp>

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace teserra::test {

struct FakeGlState {
    std::string version = "4.6.0 Fake";
    std::string renderer = "Fake renderer";
    std::string vendor = "Teserra tests";
    std::vector<std::string> extensions{"GL_KHR_debug"};
    std::map<GLenum, GLint> integers{
        {GL_MAX_UNIFORM_BUFFER_BINDINGS, 84}, {GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, 192},
        {GL_MAX_TEXTURE_SIZE, 16384},         {GL_MAX_ARRAY_TEXTURE_LAYERS, 2048},
        {GL_MAX_UNIFORM_BLOCK_SIZE, 65536},   {GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, 64},
    };
    GLfloat maxAnisotropy = 16.0F;
    /// Functions that getProcAddress() does not find, e.g. "glGetString".
    std::set<std::string, std::less<>> missingFunctions{};

    // What the device did.
    std::vector<std::string> calls{};
    std::set<GLenum> enabled{};
    GLenum clipOrigin = GL_LOWER_LEFT;
    GLenum clipDepthMode = GL_NEGATIVE_ONE_TO_ONE;
    GLDEBUGPROC debugCallback = nullptr;
    int finishCalls = 0;
    std::set<GLuint> liveTextures{};
    std::set<GLuint> liveFramebuffers{};
    /// The storage of each live buffer.
    std::map<GLuint, std::vector<std::byte>> buffers{};
    GLuint lastName = 0;
    /// What the next glGetError() returns, e.g. GL_OUT_OF_MEMORY.
    GLenum nextError = GL_NO_ERROR;
    /// glMapNamedBufferRange() returns null.
    bool failMapping = false;
    /// The source of each live shader object.
    std::map<GLuint, std::string> shaders{};
    /// When not empty, every compile fails with this info log.
    std::string compileLog{};
    /// When not empty, every link fails with this info log.
    std::string linkLog{};
    std::set<GLuint> livePrograms{};
    std::set<GLuint> liveVertexArrays{};
};

namespace fake_gl {

// The state of the context current on this thread.
inline thread_local FakeGlState* current = nullptr;

// Names of the constants the device uses, for readable call logs.
inline std::string name(GLenum value) {
    static const std::map<GLenum, std::string_view> kNames{
        {GL_TEXTURE_2D, "GL_TEXTURE_2D"},
        {GL_TEXTURE, "GL_TEXTURE"},
        {GL_BUFFER, "GL_BUFFER"},
        {GL_SHADER, "GL_SHADER"},
        {GL_PROGRAM, "GL_PROGRAM"},
        {GL_VERTEX_ARRAY, "GL_VERTEX_ARRAY"},
        {GL_FLOAT, "GL_FLOAT"},
        {GL_UNSIGNED_BYTE, "GL_UNSIGNED_BYTE"},
        {GL_UNSIGNED_SHORT, "GL_UNSIGNED_SHORT"},
        {GL_UNSIGNED_INT, "GL_UNSIGNED_INT"},
        {GL_CULL_FACE, "GL_CULL_FACE"},
        {GL_TRIANGLES, "GL_TRIANGLES"},
        {GL_TRIANGLE_STRIP, "GL_TRIANGLE_STRIP"},
        {GL_LINE_STRIP, "GL_LINE_STRIP"},
        {GL_FRONT, "GL_FRONT"},
        {GL_BACK, "GL_BACK"},
        {GL_FRONT_AND_BACK, "GL_FRONT_AND_BACK"},
        {GL_CCW, "GL_CCW"},
        {GL_CW, "GL_CW"},
        {GL_FILL, "GL_FILL"},
        {GL_LINE, "GL_LINE"},
        {GL_POLYGON_OFFSET_FILL, "GL_POLYGON_OFFSET_FILL"},
        {GL_POLYGON_OFFSET_LINE, "GL_POLYGON_OFFSET_LINE"},
        {GL_DEPTH_TEST, "GL_DEPTH_TEST"},
        {GL_BLEND, "GL_BLEND"},
        {GL_NEVER, "GL_NEVER"},
        {GL_LESS, "GL_LESS"},
        {GL_EQUAL, "GL_EQUAL"},
        {GL_LEQUAL, "GL_LEQUAL"},
        {GL_GREATER, "GL_GREATER"},
        {GL_NOTEQUAL, "GL_NOTEQUAL"},
        {GL_GEQUAL, "GL_GEQUAL"},
        {GL_ALWAYS, "GL_ALWAYS"},
        {GL_ZERO, "GL_ZERO"},
        {GL_ONE, "GL_ONE"},
        {GL_SRC_ALPHA, "GL_SRC_ALPHA"},
        {GL_ONE_MINUS_SRC_ALPHA, "GL_ONE_MINUS_SRC_ALPHA"},
        {GL_FUNC_ADD, "GL_FUNC_ADD"},
        {GL_FUNC_SUBTRACT, "GL_FUNC_SUBTRACT"},
        {GL_MAX, "GL_MAX"},
        {GL_VERTEX_SHADER, "GL_VERTEX_SHADER"},
        {GL_FRAGMENT_SHADER, "GL_FRAGMENT_SHADER"},
        {GL_SRGB8_ALPHA8, "GL_SRGB8_ALPHA8"},
        {GL_RGBA8, "GL_RGBA8"},
        {GL_RGBA16F, "GL_RGBA16F"},
        {GL_COLOR, "GL_COLOR"},
        {GL_COLOR_ATTACHMENT0, "GL_COLOR_ATTACHMENT0"},
        {GL_COLOR_ATTACHMENT1, "GL_COLOR_ATTACHMENT1"},
        {GL_DRAW_FRAMEBUFFER, "GL_DRAW_FRAMEBUFFER"},
        {GL_FRAMEBUFFER_SRGB, "GL_FRAMEBUFFER_SRGB"},
        {GL_SCISSOR_TEST, "GL_SCISSOR_TEST"},
        {GL_DEBUG_OUTPUT, "GL_DEBUG_OUTPUT"},
        {GL_DEBUG_OUTPUT_SYNCHRONOUS, "GL_DEBUG_OUTPUT_SYNCHRONOUS"},
        {GL_COLOR_BUFFER_BIT, "GL_COLOR_BUFFER_BIT"},
        {GL_NEAREST, "GL_NEAREST"},
        {GL_DEBUG_SOURCE_APPLICATION, "GL_DEBUG_SOURCE_APPLICATION"},
        {GL_DEBUG_TYPE_PUSH_GROUP, "GL_DEBUG_TYPE_PUSH_GROUP"},
        {GL_DEBUG_TYPE_POP_GROUP, "GL_DEBUG_TYPE_POP_GROUP"},
        {GL_DONT_CARE, "GL_DONT_CARE"},
        {GL_UNIFORM_BUFFER, "GL_UNIFORM_BUFFER"},
    };
    const auto found = kNames.find(value);
    return found != kNames.end() ? std::string(found->second) : std::format("0x{:X}", value);
}

// GL_*_BIT flags of buffer storage and mapping, e.g. "GL_MAP_READ_BIT|GL_MAP_PERSISTENT_BIT".
inline std::string bufferFlags(GLbitfield flags) {
    static const std::array<std::pair<GLbitfield, std::string_view>, 6> kBits{{
        {GL_DYNAMIC_STORAGE_BIT, "GL_DYNAMIC_STORAGE_BIT"},
        {GL_MAP_READ_BIT, "GL_MAP_READ_BIT"},
        {GL_MAP_WRITE_BIT, "GL_MAP_WRITE_BIT"},
        {GL_MAP_PERSISTENT_BIT, "GL_MAP_PERSISTENT_BIT"},
        {GL_MAP_COHERENT_BIT, "GL_MAP_COHERENT_BIT"},
        {GL_CLIENT_STORAGE_BIT, "GL_CLIENT_STORAGE_BIT"},
    }};
    std::string text;
    for (const auto& [bit, bitName] : kBits) {
        if ((flags & bit) != 0U) {
            text += text.empty() ? "" : "|";
            text += bitName;
        }
    }
    return text.empty() ? "0" : text;
}

inline void log(std::string call) {
    current->calls.push_back(std::move(call));
}

template <typename T>
std::string join(std::span<const T> values) {
    std::string text;
    for (const T& value : values) {
        if (!text.empty()) {
            text += ' ';
        }
        if constexpr (std::is_same_v<T, GLenum>) {
            text += name(value);
        } else {
            text += std::format("{}", value);
        }
    }
    return text;
}

// Queries

inline const GLubyte* GLAD_API_PTR getString(GLenum which) {
    const std::string* text = nullptr;
    switch (which) {
        case GL_VERSION:
            text = &current->version;
            break;
        case GL_RENDERER:
            text = &current->renderer;
            break;
        case GL_VENDOR:
            text = &current->vendor;
            break;
        default:
            return nullptr;
    }
    return reinterpret_cast<const GLubyte*>(text->c_str());
}

inline const GLubyte* GLAD_API_PTR getStringi(GLenum which, GLuint index) {
    if (which != GL_EXTENSIONS || index >= current->extensions.size()) {
        return nullptr;
    }
    return reinterpret_cast<const GLubyte*>(current->extensions[index].c_str());
}

inline void GLAD_API_PTR getIntegerv(GLenum which, GLint* value) {
    if (which == GL_NUM_EXTENSIONS) {
        *value = static_cast<GLint>(current->extensions.size());
        return;
    }
    const auto found = current->integers.find(which);
    *value = found != current->integers.end() ? found->second : 0;
}

inline void GLAD_API_PTR getFloatv(GLenum which, GLfloat* value) {
    *value = which == GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT ? current->maxAnisotropy : 0.0F;
}

// State

inline void GLAD_API_PTR enable(GLenum capability) {
    current->enabled.insert(capability);
    log(std::format("Enable({})", name(capability)));
}

inline void GLAD_API_PTR disable(GLenum capability) {
    current->enabled.erase(capability);
    log(std::format("Disable({})", name(capability)));
}

inline void GLAD_API_PTR clipControl(GLenum origin, GLenum depth) {
    current->clipOrigin = origin;
    current->clipDepthMode = depth;
}

inline void GLAD_API_PTR debugMessageCallback(GLDEBUGPROC callback, const void* /*userParam*/) {
    current->debugCallback = callback;
}

inline void GLAD_API_PTR debugMessageControl(GLenum source, GLenum type, GLenum severity, GLsizei /*count*/,
                                             const GLuint* /*ids*/, GLboolean enabled) {
    log(std::format("DebugMessageControl({}, {}, {}, {})", name(source), name(type), name(severity),
                    enabled != 0U ? "on" : "off"));
}

inline GLenum GLAD_API_PTR getError() {
    return std::exchange(current->nextError, static_cast<GLenum>(GL_NO_ERROR));
}

inline void GLAD_API_PTR finish() {
    ++current->finishCalls;
}

inline void GLAD_API_PTR viewportIndexedf(GLuint index, GLfloat x, GLfloat y, GLfloat width, GLfloat height) {
    log(std::format("Viewport({}, {} {} {} {})", index, x, y, width, height));
}

inline void GLAD_API_PTR depthRangef(GLfloat nearValue, GLfloat farValue) {
    log(std::format("DepthRange({} {})", nearValue, farValue));
}

inline void GLAD_API_PTR scissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    log(std::format("Scissor({} {} {} {})", x, y, width, height));
}

inline void GLAD_API_PTR pushDebugGroup(GLenum source, GLuint /*id*/, GLsizei length, const GLchar* message) {
    log(std::format("PushDebugGroup({}, {})", name(source),
                    std::string_view(message, static_cast<std::size_t>(length))));
}

inline void GLAD_API_PTR popDebugGroup() {
    log("PopDebugGroup()");
}

// Textures

inline void GLAD_API_PTR createTextures(GLenum target, GLsizei count, GLuint* textures) {
    for (GLsizei i = 0; i < count; ++i) {
        textures[i] = ++current->lastName;
        current->liveTextures.insert(textures[i]);
        log(std::format("CreateTextures({}) = {}", name(target), textures[i]));
    }
}

inline void GLAD_API_PTR textureStorage2D(GLuint texture, GLsizei levels, GLenum format, GLsizei width,
                                          GLsizei height) {
    log(std::format("TextureStorage2D({}, {}, {}, {}x{})", texture, levels, name(format), width, height));
}

inline void GLAD_API_PTR objectLabel(GLenum identifier, GLuint object, GLsizei length, const GLchar* label) {
    log(std::format("ObjectLabel({}, {}, {})", name(identifier), object,
                    std::string_view(label, static_cast<std::size_t>(length))));
}

inline void GLAD_API_PTR deleteTextures(GLsizei count, const GLuint* textures) {
    for (GLsizei i = 0; i < count; ++i) {
        current->liveTextures.erase(textures[i]);
        log(std::format("DeleteTextures({})", textures[i]));
    }
}

// Buffers

inline void GLAD_API_PTR createBuffers(GLsizei count, GLuint* buffers) {
    for (GLsizei i = 0; i < count; ++i) {
        buffers[i] = ++current->lastName;
        current->buffers[buffers[i]];
        log(std::format("CreateBuffers() = {}", buffers[i]));
    }
}

inline void GLAD_API_PTR namedBufferStorage(GLuint buffer, GLsizeiptr size, const void* /*data*/,
                                            GLbitfield flags) {
    current->buffers.at(buffer).assign(static_cast<std::size_t>(size), std::byte{0});
    log(std::format("NamedBufferStorage({}, {}, {})", buffer, size, bufferFlags(flags)));
}

inline void* GLAD_API_PTR mapNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length,
                                              GLbitfield access) {
    log(std::format("MapNamedBufferRange({}, {}, {}, {})", buffer, offset, length, bufferFlags(access)));
    return current->failMapping ? nullptr : current->buffers.at(buffer).data() + offset;
}

inline void GLAD_API_PTR namedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size,
                                            const void* data) {
    std::memcpy(current->buffers.at(buffer).data() + offset, data, static_cast<std::size_t>(size));
    log(std::format("NamedBufferSubData({}, {}, {})", buffer, offset, size));
}

inline void GLAD_API_PTR copyNamedBufferSubData(GLuint source, GLuint destination, GLintptr sourceOffset,
                                                GLintptr destinationOffset, GLsizeiptr size) {
    std::memcpy(current->buffers.at(destination).data() + destinationOffset,
                current->buffers.at(source).data() + sourceOffset, static_cast<std::size_t>(size));
    log(std::format("CopyNamedBufferSubData({} -> {}, {} -> {}, {})", source, destination, sourceOffset,
                    destinationOffset, size));
}

inline void GLAD_API_PTR deleteBuffers(GLsizei count, const GLuint* buffers) {
    for (GLsizei i = 0; i < count; ++i) {
        current->buffers.erase(buffers[i]);
        log(std::format("DeleteBuffers({})", buffers[i]));
    }
}

// Shaders

inline GLuint GLAD_API_PTR createShader(GLenum type) {
    const GLuint shader = ++current->lastName;
    current->shaders[shader];
    log(std::format("CreateShader({}) = {}", name(type), shader));
    return shader;
}

inline void GLAD_API_PTR shaderSource(GLuint shader, GLsizei count, const GLchar* const* strings,
                                      const GLint* lengths) {
    std::string& source = current->shaders.at(shader);
    source.clear();
    for (GLsizei i = 0; i < count; ++i) {
        source += lengths != nullptr ? std::string_view(strings[i], static_cast<std::size_t>(lengths[i]))
                                     : std::string_view(strings[i]);
    }
    log(std::format("ShaderSource({}, {} bytes)", shader, source.size()));
}

inline void GLAD_API_PTR compileShader(GLuint shader) {
    log(std::format("CompileShader({})", shader));
}

inline void GLAD_API_PTR getShaderiv(GLuint /*shader*/, GLenum which, GLint* value) {
    if (which == GL_COMPILE_STATUS) {
        *value = current->compileLog.empty() ? GL_TRUE : GL_FALSE;
    } else if (which == GL_INFO_LOG_LENGTH) {
        *value = current->compileLog.empty() ? 0 : static_cast<GLint>(current->compileLog.size() + 1);
    }
}

inline void GLAD_API_PTR getShaderInfoLog(GLuint /*shader*/, GLsizei size, GLsizei* length, GLchar* infoLog) {
    const std::string& text = current->compileLog;
    const auto copied = std::min(text.size(), static_cast<std::size_t>(std::max(size - 1, 0)));
    std::memcpy(infoLog, text.data(), copied);
    infoLog[copied] = '\0';
    *length = static_cast<GLsizei>(copied);
}

inline void GLAD_API_PTR deleteShader(GLuint shader) {
    current->shaders.erase(shader);
    log(std::format("DeleteShader({})", shader));
}

// Programs and vertex arrays

inline GLuint GLAD_API_PTR createProgram() {
    const GLuint program = ++current->lastName;
    current->livePrograms.insert(program);
    log(std::format("CreateProgram() = {}", program));
    return program;
}

inline void GLAD_API_PTR attachShader(GLuint program, GLuint shader) {
    log(std::format("AttachShader({}, {})", program, shader));
}

inline void GLAD_API_PTR detachShader(GLuint program, GLuint shader) {
    log(std::format("DetachShader({}, {})", program, shader));
}

inline void GLAD_API_PTR linkProgram(GLuint program) {
    log(std::format("LinkProgram({})", program));
}

inline void GLAD_API_PTR getProgramiv(GLuint /*program*/, GLenum which, GLint* value) {
    if (which == GL_LINK_STATUS) {
        *value = current->linkLog.empty() ? GL_TRUE : GL_FALSE;
    } else if (which == GL_INFO_LOG_LENGTH) {
        *value = current->linkLog.empty() ? 0 : static_cast<GLint>(current->linkLog.size() + 1);
    }
}

inline void GLAD_API_PTR getProgramInfoLog(GLuint /*program*/, GLsizei size, GLsizei* length,
                                           GLchar* infoLog) {
    const std::string& text = current->linkLog;
    const auto copied = std::min(text.size(), static_cast<std::size_t>(std::max(size - 1, 0)));
    std::memcpy(infoLog, text.data(), copied);
    infoLog[copied] = '\0';
    *length = static_cast<GLsizei>(copied);
}

inline void GLAD_API_PTR deleteProgram(GLuint program) {
    current->livePrograms.erase(program);
    log(std::format("DeleteProgram({})", program));
}

inline void GLAD_API_PTR useProgram(GLuint program) {
    log(std::format("UseProgram({})", program));
}

inline void GLAD_API_PTR createVertexArrays(GLsizei count, GLuint* arrays) {
    for (GLsizei i = 0; i < count; ++i) {
        arrays[i] = ++current->lastName;
        current->liveVertexArrays.insert(arrays[i]);
        log(std::format("CreateVertexArrays() = {}", arrays[i]));
    }
}

inline void GLAD_API_PTR enableVertexArrayAttrib(GLuint vertexArray, GLuint index) {
    log(std::format("EnableVertexArrayAttrib({}, {})", vertexArray, index));
}

inline void GLAD_API_PTR vertexArrayAttribFormat(GLuint vertexArray, GLuint index, GLint size, GLenum type,
                                                 GLboolean normalized, GLuint offset) {
    log(std::format("VertexArrayAttribFormat({}, {}, {} {}{}, offset {})", vertexArray, index, size,
                    name(type), normalized != 0U ? " normalized" : "", offset));
}

inline void GLAD_API_PTR vertexArrayAttribIFormat(GLuint vertexArray, GLuint index, GLint size, GLenum type,
                                                  GLuint offset) {
    log(std::format("VertexArrayAttribIFormat({}, {}, {} {}, offset {})", vertexArray, index, size,
                    name(type), offset));
}

inline void GLAD_API_PTR vertexArrayAttribBinding(GLuint vertexArray, GLuint index, GLuint binding) {
    log(std::format("VertexArrayAttribBinding({}, {}, {})", vertexArray, index, binding));
}

inline void GLAD_API_PTR vertexArrayBindingDivisor(GLuint vertexArray, GLuint binding, GLuint divisor) {
    log(std::format("VertexArrayBindingDivisor({}, {}, {})", vertexArray, binding, divisor));
}

inline void GLAD_API_PTR vertexArrayVertexBuffer(GLuint vertexArray, GLuint binding, GLuint buffer,
                                                 GLintptr offset, GLsizei stride) {
    log(std::format("VertexArrayVertexBuffer({}, {}, {}, offset {}, stride {})", vertexArray, binding, buffer,
                    offset, stride));
}

inline void GLAD_API_PTR vertexArrayElementBuffer(GLuint vertexArray, GLuint buffer) {
    log(std::format("VertexArrayElementBuffer({}, {})", vertexArray, buffer));
}

inline void GLAD_API_PTR bindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset,
                                         GLsizeiptr size) {
    log(std::format("BindBufferRange({}, {}, {}, offset {}, size {})", name(target), index, buffer, offset,
                    size));
}

inline void GLAD_API_PTR bindVertexArray(GLuint vertexArray) {
    log(std::format("BindVertexArray({})", vertexArray));
}

inline void GLAD_API_PTR deleteVertexArrays(GLsizei count, const GLuint* arrays) {
    for (GLsizei i = 0; i < count; ++i) {
        current->liveVertexArrays.erase(arrays[i]);
        log(std::format("DeleteVertexArrays({})", arrays[i]));
    }
}

// Draws

inline void GLAD_API_PTR drawArraysInstancedBaseInstance(GLenum mode, GLint first, GLsizei count,
                                                         GLsizei instances, GLuint baseInstance) {
    log(std::format("DrawArrays({}, first {}, count {}, instances {} from {})", name(mode), first, count,
                    instances, baseInstance));
}

inline void GLAD_API_PTR drawElementsInstancedBaseVertexBaseInstance(GLenum mode, GLsizei count, GLenum type,
                                                                     const void* indices, GLsizei instances,
                                                                     GLint baseVertex, GLuint baseInstance) {
    log(std::format("DrawElements({}, count {}, {}, offset {}, instances {} from {}, base vertex {})",
                    name(mode), count, name(type), reinterpret_cast<std::uintptr_t>(indices), instances,
                    baseInstance, baseVertex));
}

// Fixed-function state

inline void GLAD_API_PTR cullFace(GLenum face) {
    log(std::format("CullFace({})", name(face)));
}

inline void GLAD_API_PTR frontFace(GLenum winding) {
    log(std::format("FrontFace({})", name(winding)));
}

inline void GLAD_API_PTR polygonMode(GLenum face, GLenum mode) {
    log(std::format("PolygonMode({}, {})", name(face), name(mode)));
}

inline void GLAD_API_PTR polygonOffset(GLfloat factor, GLfloat units) {
    log(std::format("PolygonOffset({} {})", factor, units));
}

inline void GLAD_API_PTR depthMask(GLboolean enabled) {
    log(std::format("DepthMask({})", enabled != 0U ? "on" : "off"));
}

inline void GLAD_API_PTR depthFunc(GLenum function) {
    log(std::format("DepthFunc({})", name(function)));
}

inline void GLAD_API_PTR enablei(GLenum capability, GLuint index) {
    log(std::format("Enablei({}, {})", name(capability), index));
}

inline void GLAD_API_PTR disablei(GLenum capability, GLuint index) {
    log(std::format("Disablei({}, {})", name(capability), index));
}

inline void GLAD_API_PTR blendFuncSeparatei(GLuint target, GLenum srcColor, GLenum dstColor, GLenum srcAlpha,
                                            GLenum dstAlpha) {
    log(std::format("BlendFuncSeparatei({}, {} {} {} {})", target, name(srcColor), name(dstColor),
                    name(srcAlpha), name(dstAlpha)));
}

inline void GLAD_API_PTR blendEquationSeparatei(GLuint target, GLenum color, GLenum alpha) {
    log(std::format("BlendEquationSeparatei({}, {} {})", target, name(color), name(alpha)));
}

inline void GLAD_API_PTR colorMaski(GLuint target, GLboolean red, GLboolean green, GLboolean blue,
                                    GLboolean alpha) {
    log(std::format("ColorMaski({}, {}{}{}{})", target, red != 0U ? "R" : "-", green != 0U ? "G" : "-",
                    blue != 0U ? "B" : "-", alpha != 0U ? "A" : "-"));
}

// Framebuffers

inline void GLAD_API_PTR createFramebuffers(GLsizei count, GLuint* framebuffers) {
    for (GLsizei i = 0; i < count; ++i) {
        framebuffers[i] = ++current->lastName;
        current->liveFramebuffers.insert(framebuffers[i]);
        log(std::format("CreateFramebuffers() = {}", framebuffers[i]));
    }
}

inline void GLAD_API_PTR namedFramebufferTexture(GLuint framebuffer, GLenum attachment, GLuint texture,
                                                 GLint level) {
    log(std::format("NamedFramebufferTexture({}, {}, {}, {})", framebuffer, name(attachment), texture,
                    level));
}

inline void GLAD_API_PTR namedFramebufferDrawBuffers(GLuint framebuffer, GLsizei count,
                                                     const GLenum* buffers) {
    log(std::format("NamedFramebufferDrawBuffers({}, {})", framebuffer,
                    join(std::span(buffers, static_cast<std::size_t>(count)))));
}

inline GLenum GLAD_API_PTR checkNamedFramebufferStatus(GLuint /*framebuffer*/, GLenum /*target*/) {
    return GL_FRAMEBUFFER_COMPLETE;
}

inline void GLAD_API_PTR deleteFramebuffers(GLsizei count, const GLuint* framebuffers) {
    for (GLsizei i = 0; i < count; ++i) {
        current->liveFramebuffers.erase(framebuffers[i]);
        log(std::format("DeleteFramebuffers({})", framebuffers[i]));
    }
}

inline void GLAD_API_PTR bindFramebuffer(GLenum target, GLuint framebuffer) {
    log(std::format("BindFramebuffer({}, {})", name(target), framebuffer));
}

inline void GLAD_API_PTR clearNamedFramebufferfv(GLuint framebuffer, GLenum buffer, GLint drawBuffer,
                                                 const GLfloat* value) {
    log(std::format("ClearNamedFramebufferfv({}, {}, {}, {})", framebuffer, name(buffer), drawBuffer,
                    join(std::span(value, 4))));
}

inline void GLAD_API_PTR invalidateNamedFramebufferData(GLuint framebuffer, GLsizei count,
                                                        const GLenum* attachments) {
    log(std::format("InvalidateNamedFramebufferData({}, {})", framebuffer,
                    join(std::span(attachments, static_cast<std::size_t>(count)))));
}

inline void GLAD_API_PTR blitNamedFramebuffer(GLuint source, GLuint destination, GLint srcX0, GLint srcY0,
                                              GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1,
                                              GLint dstY1, GLbitfield mask, GLenum filter) {
    log(std::format("BlitNamedFramebuffer({} -> {}, {} {} {} {} -> {} {} {} {}, {}, {})", source, destination,
                    srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, name(mask), name(filter)));
}

} // namespace fake_gl

class FakeGlContext final : public platform::OpenGlContext {
public:
    FakeGlState state;

    void makeCurrent() override { fake_gl::current = &state; }
    void swapBuffers() override { state.calls.emplace_back("SwapBuffers()"); }
    void setSwapInterval(int interval) override {
        state.calls.push_back(std::format("SwapInterval({})", interval));
    }

    [[nodiscard]] platform::GlProc getProcAddress(const char* name) override {
        // Function pointers travel as GlProc, as a real driver's do.
        static const std::map<std::string_view, platform::GlProc> kFunctions{
            {"glGetString", reinterpret_cast<platform::GlProc>(&fake_gl::getString)},
            {"glGetStringi", reinterpret_cast<platform::GlProc>(&fake_gl::getStringi)},
            {"glGetIntegerv", reinterpret_cast<platform::GlProc>(&fake_gl::getIntegerv)},
            {"glGetFloatv", reinterpret_cast<platform::GlProc>(&fake_gl::getFloatv)},
            {"glEnable", reinterpret_cast<platform::GlProc>(&fake_gl::enable)},
            {"glDisable", reinterpret_cast<platform::GlProc>(&fake_gl::disable)},
            {"glClipControl", reinterpret_cast<platform::GlProc>(&fake_gl::clipControl)},
            {"glDebugMessageCallback", reinterpret_cast<platform::GlProc>(&fake_gl::debugMessageCallback)},
            {"glDebugMessageControl", reinterpret_cast<platform::GlProc>(&fake_gl::debugMessageControl)},
            {"glGetError", reinterpret_cast<platform::GlProc>(&fake_gl::getError)},
            {"glFinish", reinterpret_cast<platform::GlProc>(&fake_gl::finish)},
            {"glViewportIndexedf", reinterpret_cast<platform::GlProc>(&fake_gl::viewportIndexedf)},
            {"glDepthRangef", reinterpret_cast<platform::GlProc>(&fake_gl::depthRangef)},
            {"glScissor", reinterpret_cast<platform::GlProc>(&fake_gl::scissor)},
            {"glPushDebugGroup", reinterpret_cast<platform::GlProc>(&fake_gl::pushDebugGroup)},
            {"glPopDebugGroup", reinterpret_cast<platform::GlProc>(&fake_gl::popDebugGroup)},
            {"glCreateTextures", reinterpret_cast<platform::GlProc>(&fake_gl::createTextures)},
            {"glTextureStorage2D", reinterpret_cast<platform::GlProc>(&fake_gl::textureStorage2D)},
            {"glObjectLabel", reinterpret_cast<platform::GlProc>(&fake_gl::objectLabel)},
            {"glDeleteTextures", reinterpret_cast<platform::GlProc>(&fake_gl::deleteTextures)},
            {"glCreateBuffers", reinterpret_cast<platform::GlProc>(&fake_gl::createBuffers)},
            {"glNamedBufferStorage", reinterpret_cast<platform::GlProc>(&fake_gl::namedBufferStorage)},
            {"glMapNamedBufferRange", reinterpret_cast<platform::GlProc>(&fake_gl::mapNamedBufferRange)},
            {"glNamedBufferSubData", reinterpret_cast<platform::GlProc>(&fake_gl::namedBufferSubData)},
            {"glCopyNamedBufferSubData",
             reinterpret_cast<platform::GlProc>(&fake_gl::copyNamedBufferSubData)},
            {"glDeleteBuffers", reinterpret_cast<platform::GlProc>(&fake_gl::deleteBuffers)},
            {"glCreateShader", reinterpret_cast<platform::GlProc>(&fake_gl::createShader)},
            {"glShaderSource", reinterpret_cast<platform::GlProc>(&fake_gl::shaderSource)},
            {"glCompileShader", reinterpret_cast<platform::GlProc>(&fake_gl::compileShader)},
            {"glGetShaderiv", reinterpret_cast<platform::GlProc>(&fake_gl::getShaderiv)},
            {"glGetShaderInfoLog", reinterpret_cast<platform::GlProc>(&fake_gl::getShaderInfoLog)},
            {"glDeleteShader", reinterpret_cast<platform::GlProc>(&fake_gl::deleteShader)},
            {"glCreateProgram", reinterpret_cast<platform::GlProc>(&fake_gl::createProgram)},
            {"glAttachShader", reinterpret_cast<platform::GlProc>(&fake_gl::attachShader)},
            {"glDetachShader", reinterpret_cast<platform::GlProc>(&fake_gl::detachShader)},
            {"glLinkProgram", reinterpret_cast<platform::GlProc>(&fake_gl::linkProgram)},
            {"glGetProgramiv", reinterpret_cast<platform::GlProc>(&fake_gl::getProgramiv)},
            {"glGetProgramInfoLog", reinterpret_cast<platform::GlProc>(&fake_gl::getProgramInfoLog)},
            {"glDeleteProgram", reinterpret_cast<platform::GlProc>(&fake_gl::deleteProgram)},
            {"glUseProgram", reinterpret_cast<platform::GlProc>(&fake_gl::useProgram)},
            {"glCreateVertexArrays", reinterpret_cast<platform::GlProc>(&fake_gl::createVertexArrays)},
            {"glEnableVertexArrayAttrib",
             reinterpret_cast<platform::GlProc>(&fake_gl::enableVertexArrayAttrib)},
            {"glVertexArrayAttribFormat",
             reinterpret_cast<platform::GlProc>(&fake_gl::vertexArrayAttribFormat)},
            {"glVertexArrayAttribIFormat",
             reinterpret_cast<platform::GlProc>(&fake_gl::vertexArrayAttribIFormat)},
            {"glVertexArrayAttribBinding",
             reinterpret_cast<platform::GlProc>(&fake_gl::vertexArrayAttribBinding)},
            {"glVertexArrayBindingDivisor",
             reinterpret_cast<platform::GlProc>(&fake_gl::vertexArrayBindingDivisor)},
            {"glBindVertexArray", reinterpret_cast<platform::GlProc>(&fake_gl::bindVertexArray)},
            {"glVertexArrayVertexBuffer",
             reinterpret_cast<platform::GlProc>(&fake_gl::vertexArrayVertexBuffer)},
            {"glVertexArrayElementBuffer",
             reinterpret_cast<platform::GlProc>(&fake_gl::vertexArrayElementBuffer)},
            {"glBindBufferRange", reinterpret_cast<platform::GlProc>(&fake_gl::bindBufferRange)},
            {"glDrawArraysInstancedBaseInstance",
             reinterpret_cast<platform::GlProc>(&fake_gl::drawArraysInstancedBaseInstance)},
            {"glDrawElementsInstancedBaseVertexBaseInstance",
             reinterpret_cast<platform::GlProc>(&fake_gl::drawElementsInstancedBaseVertexBaseInstance)},
            {"glDeleteVertexArrays", reinterpret_cast<platform::GlProc>(&fake_gl::deleteVertexArrays)},
            {"glCullFace", reinterpret_cast<platform::GlProc>(&fake_gl::cullFace)},
            {"glFrontFace", reinterpret_cast<platform::GlProc>(&fake_gl::frontFace)},
            {"glPolygonMode", reinterpret_cast<platform::GlProc>(&fake_gl::polygonMode)},
            {"glPolygonOffset", reinterpret_cast<platform::GlProc>(&fake_gl::polygonOffset)},
            {"glDepthMask", reinterpret_cast<platform::GlProc>(&fake_gl::depthMask)},
            {"glDepthFunc", reinterpret_cast<platform::GlProc>(&fake_gl::depthFunc)},
            {"glEnablei", reinterpret_cast<platform::GlProc>(&fake_gl::enablei)},
            {"glDisablei", reinterpret_cast<platform::GlProc>(&fake_gl::disablei)},
            {"glBlendFuncSeparatei", reinterpret_cast<platform::GlProc>(&fake_gl::blendFuncSeparatei)},
            {"glBlendEquationSeparatei",
             reinterpret_cast<platform::GlProc>(&fake_gl::blendEquationSeparatei)},
            {"glColorMaski", reinterpret_cast<platform::GlProc>(&fake_gl::colorMaski)},
            {"glCreateFramebuffers", reinterpret_cast<platform::GlProc>(&fake_gl::createFramebuffers)},
            {"glNamedFramebufferTexture",
             reinterpret_cast<platform::GlProc>(&fake_gl::namedFramebufferTexture)},
            {"glNamedFramebufferDrawBuffers",
             reinterpret_cast<platform::GlProc>(&fake_gl::namedFramebufferDrawBuffers)},
            {"glCheckNamedFramebufferStatus",
             reinterpret_cast<platform::GlProc>(&fake_gl::checkNamedFramebufferStatus)},
            {"glDeleteFramebuffers", reinterpret_cast<platform::GlProc>(&fake_gl::deleteFramebuffers)},
            {"glBindFramebuffer", reinterpret_cast<platform::GlProc>(&fake_gl::bindFramebuffer)},
            {"glClearNamedFramebufferfv",
             reinterpret_cast<platform::GlProc>(&fake_gl::clearNamedFramebufferfv)},
            {"glInvalidateNamedFramebufferData",
             reinterpret_cast<platform::GlProc>(&fake_gl::invalidateNamedFramebufferData)},
            {"glBlitNamedFramebuffer", reinterpret_cast<platform::GlProc>(&fake_gl::blitNamedFramebuffer)},
        };
        const std::string_view function = name;
        if (state.missingFunctions.contains(function)) {
            return nullptr;
        }
        const auto found = kFunctions.find(function);
        return found != kFunctions.end() ? found->second : nullptr;
    }
};

/// A window whose OpenGL context is a FakeGlContext, or that has none.
class FakeGlWindow final : public platform::Window {
public:
    explicit FakeGlWindow(bool withContext = true, UVec2 framebufferSize = {320, 200})
        : m_withContext(withContext), m_framebufferSize(framebufferSize) {}

    FakeGlContext context;

    [[nodiscard]] UVec2 windowSize() const override { return m_framebufferSize; }
    [[nodiscard]] UVec2 framebufferSize() const override { return m_framebufferSize; }
    [[nodiscard]] Vec2 contentScale() const override { return Vec2{1.0F}; }
    void setSize(UVec2 /*size*/) override {}
    void setTitle(std::string_view /*title*/) override {}
    [[nodiscard]] bool shouldClose() const override { return false; }
    [[nodiscard]] bool isFocused() const override { return false; }
    [[nodiscard]] platform::NativeWindowHandle nativeHandle() const override { return {}; }
    [[nodiscard]] platform::OpenGlContext* openGlContext() override {
        return m_withContext ? &context : nullptr;
    }

private:
    bool m_withContext;
    UVec2 m_framebufferSize;
};

} // namespace teserra::test
