#pragma once

// Fixed-function state of a graphics pipeline as GL values, and the cache that applies it
// (docs/architecture/rhi.md#opengl-backend-mapping). GlStateCache is the only code that changes this state,
// so it always knows what GL has and binding a pipeline issues only the calls whose values differ.

#include <teserra/core/types.hpp>
#include <teserra/rhi/limits.hpp>
#include <teserra/rhi/pipeline.hpp>

#include <glad/gl.h>

#include <array>

namespace teserra::rhi::opengl {

/// Blending and write mask of one color target.
struct GlBlendTarget {
    bool enabled = false;
    GLenum srcColor = GL_ONE;
    GLenum dstColor = GL_ZERO;
    GLenum colorOp = GL_FUNC_ADD;
    GLenum srcAlpha = GL_ONE;
    GLenum dstAlpha = GL_ZERO;
    GLenum alphaOp = GL_FUNC_ADD;
    std::array<GLboolean, 4> writeMask{GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};

    friend bool operator==(const GlBlendTarget&, const GlBlendTarget&) = default;
};

/// What a pipeline sets besides its program and vertex array.
struct GlPipelineState {
    bool cullEnabled = false;
    GLenum cullFace = GL_BACK;
    GLenum frontFace = GL_CCW;
    GLenum polygonMode = GL_FILL;
    bool polygonOffset = false;
    f32 offsetFactor = 0.0F;
    f32 offsetUnits = 0.0F;
    bool depthTest = false;
    bool depthWrite = false;
    GLenum depthFunc = GL_LESS;
    std::array<GlBlendTarget, kMaxColorAttachments> blend{};

    friend bool operator==(const GlPipelineState&, const GlPipelineState&) = default;
};

/// The GL state of a pipeline's raster, depth and blend state.
[[nodiscard]] GlPipelineState capturePipelineState(const GraphicsPipelineDesc& desc);

/// The GL primitive mode of a topology, e.g. GL_TRIANGLES.
[[nodiscard]] GLenum glPrimitiveMode(PrimitiveTopology topology) noexcept;

/// The glFrontFace() value for the RHI's front faces (ADR-0024).
[[nodiscard]] GLenum glWinding(FrontFace frontFace) noexcept;

/// The GL compare function of a CompareOp, e.g. GL_LEQUAL.
[[nodiscard]] GLenum glCompareFunction(CompareOp op) noexcept;

/// Applies pipeline state, issuing only the GL calls whose values differ from what is set.
class GlStateCache {
public:
    explicit GlStateCache(const GladGLContext& gl)
        : m_gl(gl) {}

    /// Sets every piece of state to the defaults of GlPipelineState, no program and no vertex array, so that
    /// the cache knows all of GL's state from then on. Called once, when the device is set up.
    void reset();
    /// Makes `program`, `vertexArray` and `state` current.
    void bind(GLuint program, GLuint vertexArray, const GlPipelineState& state);
    /// Lets clears of the first `count` color targets write every component: GL masks clears too.
    void prepareColorClear(u32 count);
    /// Lets depth clears write: GL masks them with the depth write mask.
    void prepareDepthClear();
    /// Unbinds a program and a vertex array that are about to be deleted if they are current, so that GL and
    /// the cache never refer to a deleted object, nor mistake a new object with the same name for it. 0 is
    /// ignored.
    void unbind(GLuint program, GLuint vertexArray);

private:
    void setCapability(GLenum capability, bool enabled);
    void applyBlend(u32 target, const GlBlendTarget& blend, bool force);

    const GladGLContext& m_gl;
    GLuint m_program = 0;
    GLuint m_vertexArray = 0;
    GlPipelineState m_state{};
};

} // namespace teserra::rhi::opengl
