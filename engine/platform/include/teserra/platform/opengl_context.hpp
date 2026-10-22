#pragma once

// The only OpenGL-aware interface outside the RHI's OpenGL backend (ADR-0006). It contains no GL types:
// the backend loads GL functions through getProcAddress() (with glad, ADR-0009).

namespace teserra::platform {

/// An OpenGL function pointer, before the caller casts it to the function's real type.
using GlProc = void (*)();

/// OpenGL 4.5 core context of a window, created with WindowDesc::graphicsApi = GraphicsApi::OpenGL and
/// owned by that window. Main thread only.
class OpenGlContext {
public:
    virtual ~OpenGlContext() = default;

    /// Makes this context current on the calling thread.
    virtual void makeCurrent() = 0;

    /// Presents the back buffer.
    virtual void swapBuffers() = 0;

    /// Number of vertical blanks to wait for in swapBuffers(): 0 disables vsync, 1 enables it. Applies to
    /// the current context.
    virtual void setSwapInterval(int interval) = 0;

    /// Address of an OpenGL function or extension function, or nullptr if it does not exist. The context
    /// must be current.
    [[nodiscard]] virtual GlProc getProcAddress(const char* name) = 0;

protected:
    OpenGlContext() = default;
    OpenGlContext(const OpenGlContext&) = default;
    OpenGlContext& operator=(const OpenGlContext&) = default;
    OpenGlContext(OpenGlContext&&) = default;
    OpenGlContext& operator=(OpenGlContext&&) = default;
};

} // namespace teserra::platform
