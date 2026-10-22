#pragma once

// A fake OpenGL driver for tests without a GPU. FakeGlContext::getProcAddress() returns the fake functions
// below, which, like a real driver, act on the context made current last on the calling thread: they report
// its version, strings, extensions and limits, and record what the device enables and sets.

#include <teserra/core/math.hpp>
#include <teserra/platform/opengl_context.hpp>
#include <teserra/platform/window.hpp>

#include <glad/gl.h>

#include <map>
#include <set>
#include <string>
#include <string_view>
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

    // Recorded calls.
    std::set<GLenum> enabled{};
    GLenum clipOrigin = GL_LOWER_LEFT;
    GLenum clipDepthMode = GL_NEGATIVE_ONE_TO_ONE;
    GLDEBUGPROC debugCallback = nullptr;
    int finishCalls = 0;
};

namespace fake_gl {

// The state of the context current on this thread.
inline thread_local FakeGlState* current = nullptr;

inline const GLubyte* GLAD_API_PTR getString(GLenum name) {
    const std::string* text = nullptr;
    switch (name) {
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

inline const GLubyte* GLAD_API_PTR getStringi(GLenum name, GLuint index) {
    if (name != GL_EXTENSIONS || index >= current->extensions.size()) {
        return nullptr;
    }
    return reinterpret_cast<const GLubyte*>(current->extensions[index].c_str());
}

inline void GLAD_API_PTR getIntegerv(GLenum name, GLint* value) {
    if (name == GL_NUM_EXTENSIONS) {
        *value = static_cast<GLint>(current->extensions.size());
        return;
    }
    const auto found = current->integers.find(name);
    *value = found != current->integers.end() ? found->second : 0;
}

inline void GLAD_API_PTR getFloatv(GLenum name, GLfloat* value) {
    *value = name == GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT ? current->maxAnisotropy : 0.0F;
}

inline void GLAD_API_PTR enable(GLenum capability) {
    current->enabled.insert(capability);
}

inline void GLAD_API_PTR clipControl(GLenum origin, GLenum depth) {
    current->clipOrigin = origin;
    current->clipDepthMode = depth;
}

inline void GLAD_API_PTR debugMessageCallback(GLDEBUGPROC callback, const void* /*userParam*/) {
    current->debugCallback = callback;
}

inline void GLAD_API_PTR finish() {
    ++current->finishCalls;
}

} // namespace fake_gl

class FakeGlContext final : public platform::OpenGlContext {
public:
    FakeGlState state;

    void makeCurrent() override { fake_gl::current = &state; }
    void swapBuffers() override {}
    void setSwapInterval(int /*interval*/) override {}

    [[nodiscard]] platform::GlProc getProcAddress(const char* name) override {
        const std::string_view function = name;
        if (state.missingFunctions.contains(function)) {
            return nullptr;
        }
        // Function pointers are cast to GlProc for transport, as a real driver's are.
        if (function == "glGetString") {
            return reinterpret_cast<platform::GlProc>(&fake_gl::getString);
        }
        if (function == "glGetStringi") {
            return reinterpret_cast<platform::GlProc>(&fake_gl::getStringi);
        }
        if (function == "glGetIntegerv") {
            return reinterpret_cast<platform::GlProc>(&fake_gl::getIntegerv);
        }
        if (function == "glGetFloatv") {
            return reinterpret_cast<platform::GlProc>(&fake_gl::getFloatv);
        }
        if (function == "glEnable") {
            return reinterpret_cast<platform::GlProc>(&fake_gl::enable);
        }
        if (function == "glClipControl") {
            return reinterpret_cast<platform::GlProc>(&fake_gl::clipControl);
        }
        if (function == "glDebugMessageCallback") {
            return reinterpret_cast<platform::GlProc>(&fake_gl::debugMessageCallback);
        }
        if (function == "glFinish") {
            return reinterpret_cast<platform::GlProc>(&fake_gl::finish);
        }
        return nullptr;
    }
};

/// A window whose OpenGL context is a FakeGlContext, or that has none.
class FakeGlWindow final : public platform::Window {
public:
    explicit FakeGlWindow(bool withContext = true)
        : m_withContext(withContext) {}

    FakeGlContext context;

    [[nodiscard]] UVec2 windowSize() const override { return {320, 200}; }
    [[nodiscard]] UVec2 framebufferSize() const override { return {320, 200}; }
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
};

} // namespace teserra::test
