#pragma once

// A fake OpenGL driver for tests without a GPU. FakeGlContext::getProcAddress() returns the fake functions
// below, which, like a real driver, act on the context made current last on the calling thread: they report
// its version, strings, extensions and limits, hand out object names, and record what the device does as a
// log of calls ("ClearNamedFramebufferfv(1, GL_COLOR, 0, 0.1 0.2 0.3 1)") that tests compare.

#include <teserra/core/math.hpp>
#include <teserra/platform/opengl_context.hpp>
#include <teserra/platform/window.hpp>

#include <glad/gl.h>

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
    GLuint lastName = 0;
};

namespace fake_gl {

// The state of the context current on this thread.
inline thread_local FakeGlState* current = nullptr;

// Names of the constants the device uses, for readable call logs.
inline std::string name(GLenum value) {
    static const std::map<GLenum, std::string_view> kNames{
        {GL_TEXTURE_2D, "GL_TEXTURE_2D"},
        {GL_TEXTURE, "GL_TEXTURE"},
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
    };
    const auto found = kNames.find(value);
    return found != kNames.end() ? std::string(found->second) : std::format("0x{:X}", value);
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
