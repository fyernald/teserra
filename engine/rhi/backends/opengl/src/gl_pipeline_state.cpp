#include "gl_pipeline_state.hpp"

#include <algorithm>

namespace teserra::rhi::opengl {

namespace {

GLenum glBlendFactor(BlendFactor factor) noexcept {
    switch (factor) {
        case BlendFactor::Zero:
            return GL_ZERO;
        case BlendFactor::One:
            return GL_ONE;
        case BlendFactor::SrcColor:
            return GL_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:
            return GL_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor:
            return GL_DST_COLOR;
        case BlendFactor::OneMinusDstColor:
            return GL_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha:
            return GL_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:
            return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha:
            return GL_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:
            return GL_ONE_MINUS_DST_ALPHA;
    }
    return GL_ONE;
}

GLenum glBlendEquation(BlendOp op) noexcept {
    switch (op) {
        case BlendOp::Add:
            return GL_FUNC_ADD;
        case BlendOp::Subtract:
            return GL_FUNC_SUBTRACT;
        case BlendOp::ReverseSubtract:
            return GL_FUNC_REVERSE_SUBTRACT;
        case BlendOp::Min:
            return GL_MIN;
        case BlendOp::Max:
            return GL_MAX;
    }
    return GL_FUNC_ADD;
}

GLboolean glBoolean(bool value) noexcept {
    return value ? GL_TRUE : GL_FALSE;
}

} // namespace

GLenum glPrimitiveMode(PrimitiveTopology topology) noexcept {
    switch (topology) {
        case PrimitiveTopology::TriangleList:
            return GL_TRIANGLES;
        case PrimitiveTopology::TriangleStrip:
            return GL_TRIANGLE_STRIP;
        case PrimitiveTopology::LineList:
            return GL_LINES;
        case PrimitiveTopology::LineStrip:
            return GL_LINE_STRIP;
        case PrimitiveTopology::PointList:
            return GL_POINTS;
    }
    return GL_TRIANGLES;
}

GLenum glWinding(FrontFace frontFace) noexcept {
    // ADR-0024: front faces are counter-clockwise as seen by the viewer, with NDC +Y up. The expectation is
    // that glClipControl(GL_UPPER_LEFT) keeps a triangle's facing as it flips window y, so GL's names mean
    // what the RHI's do. The winding test of the triangle example (#35) checks this on a driver; if a driver
    // disagrees, only this function changes.
    return frontFace == FrontFace::CounterClockwise ? GL_CCW : GL_CW;
}

GLenum glCompareFunction(CompareOp op) noexcept {
    switch (op) {
        case CompareOp::Never:
            return GL_NEVER;
        case CompareOp::Less:
            return GL_LESS;
        case CompareOp::Equal:
            return GL_EQUAL;
        case CompareOp::LessOrEqual:
            return GL_LEQUAL;
        case CompareOp::Greater:
            return GL_GREATER;
        case CompareOp::NotEqual:
            return GL_NOTEQUAL;
        case CompareOp::GreaterOrEqual:
            return GL_GEQUAL;
        case CompareOp::Always:
            return GL_ALWAYS;
    }
    return GL_ALWAYS;
}

GlPipelineState capturePipelineState(const GraphicsPipelineDesc& desc) {
    GlPipelineState state;
    const RasterState& raster = desc.raster;
    state.cullEnabled = raster.cullMode != CullMode::None;
    state.cullFace = raster.cullMode == CullMode::Front ? GL_FRONT : GL_BACK;
    state.frontFace = glWinding(raster.frontFace);
    state.polygonMode = raster.fillMode == FillMode::Line ? GL_LINE : GL_FILL;
    state.polygonOffset = raster.depthBias.constantFactor != 0.0F || raster.depthBias.slopeFactor != 0.0F;
    state.offsetFactor = raster.depthBias.slopeFactor;
    state.offsetUnits = raster.depthBias.constantFactor;

    // GL writes no depth while its depth test is disabled, so "write without testing" is a test that always
    // passes.
    const DepthStencilState& depth = desc.depthStencil;
    const bool writeOnly = depth.depthWrite && !depth.depthTest;
    state.depthTest = depth.depthTest || depth.depthWrite;
    state.depthWrite = depth.depthWrite;
    state.depthFunc = writeOnly ? GL_ALWAYS : glCompareFunction(depth.depthCompare);

    for (u32 i = 0; i < kMaxColorAttachments; ++i) {
        const BlendState& blend = desc.colorBlend[i];
        GlBlendTarget& target = state.blend[i];
        target.enabled = blend.enabled;
        target.srcColor = glBlendFactor(blend.srcColor);
        target.dstColor = glBlendFactor(blend.dstColor);
        target.colorOp = glBlendEquation(blend.colorOp);
        target.srcAlpha = glBlendFactor(blend.srcAlpha);
        target.dstAlpha = glBlendFactor(blend.dstAlpha);
        target.alphaOp = glBlendEquation(blend.alphaOp);
        target.writeMask = {glBoolean(hasAll(blend.writeMask, ColorWriteMask::Red)),
                            glBoolean(hasAll(blend.writeMask, ColorWriteMask::Green)),
                            glBoolean(hasAll(blend.writeMask, ColorWriteMask::Blue)),
                            glBoolean(hasAll(blend.writeMask, ColorWriteMask::Alpha))};
    }
    return state;
}

// ---------------------------------------------------------------------------------------------------------
// GlStateCache

void GlStateCache::reset() {
    const GlPipelineState defaults{};
    m_gl.UseProgram(0);
    m_gl.BindVertexArray(0);
    m_program = 0;
    m_vertexArray = 0;
    setCapability(GL_CULL_FACE, defaults.cullEnabled);
    m_gl.CullFace(defaults.cullFace);
    m_gl.FrontFace(defaults.frontFace);
    m_gl.PolygonMode(GL_FRONT_AND_BACK, defaults.polygonMode);
    setCapability(GL_POLYGON_OFFSET_FILL, defaults.polygonOffset);
    setCapability(GL_POLYGON_OFFSET_LINE, defaults.polygonOffset);
    m_gl.PolygonOffset(defaults.offsetFactor, defaults.offsetUnits);
    setCapability(GL_DEPTH_TEST, defaults.depthTest);
    m_gl.DepthMask(glBoolean(defaults.depthWrite));
    m_gl.DepthFunc(defaults.depthFunc);
    for (u32 i = 0; i < kMaxColorAttachments; ++i) {
        applyBlend(i, defaults.blend[i], /*force=*/true);
    }
    m_state = defaults;
}

void GlStateCache::bind(GLuint program, GLuint vertexArray, const GlPipelineState& state) {
    if (program != m_program) {
        m_gl.UseProgram(program);
        m_program = program;
    }
    if (vertexArray != m_vertexArray) {
        m_gl.BindVertexArray(vertexArray);
        m_vertexArray = vertexArray;
    }
    if (state == m_state) {
        return;
    }

    const GlPipelineState& current = m_state;
    if (state.cullEnabled != current.cullEnabled) {
        setCapability(GL_CULL_FACE, state.cullEnabled);
    }
    if (state.cullFace != current.cullFace) {
        m_gl.CullFace(state.cullFace);
    }
    if (state.frontFace != current.frontFace) {
        m_gl.FrontFace(state.frontFace);
    }
    if (state.polygonMode != current.polygonMode) {
        m_gl.PolygonMode(GL_FRONT_AND_BACK, state.polygonMode);
    }
    if (state.polygonOffset != current.polygonOffset) {
        setCapability(GL_POLYGON_OFFSET_FILL, state.polygonOffset);
        setCapability(GL_POLYGON_OFFSET_LINE, state.polygonOffset);
    }
    if (state.offsetFactor != current.offsetFactor || state.offsetUnits != current.offsetUnits) {
        m_gl.PolygonOffset(state.offsetFactor, state.offsetUnits);
    }
    if (state.depthTest != current.depthTest) {
        setCapability(GL_DEPTH_TEST, state.depthTest);
    }
    if (state.depthWrite != current.depthWrite) {
        m_gl.DepthMask(glBoolean(state.depthWrite));
    }
    if (state.depthFunc != current.depthFunc) {
        m_gl.DepthFunc(state.depthFunc);
    }
    for (u32 i = 0; i < kMaxColorAttachments; ++i) {
        applyBlend(i, state.blend[i], /*force=*/false);
    }
    m_state = state;
}

void GlStateCache::prepareColorClear(u32 count) {
    constexpr std::array<GLboolean, 4> kAll{GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    for (u32 i = 0; i < std::min(count, kMaxColorAttachments); ++i) {
        std::array<GLboolean, 4>& mask = m_state.blend[i].writeMask;
        if (mask != kAll) {
            m_gl.ColorMaski(i, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            mask = kAll;
        }
    }
}

void GlStateCache::unbind(GLuint program, GLuint vertexArray) {
    if (program != 0 && program == m_program) {
        m_gl.UseProgram(0);
        m_program = 0;
    }
    if (vertexArray != 0 && vertexArray == m_vertexArray) {
        m_gl.BindVertexArray(0);
        m_vertexArray = 0;
    }
}

void GlStateCache::setCapability(GLenum capability, bool enabled) {
    if (enabled) {
        m_gl.Enable(capability);
    } else {
        m_gl.Disable(capability);
    }
}

void GlStateCache::applyBlend(u32 target, const GlBlendTarget& blend, bool force) {
    const GlBlendTarget& current = m_state.blend[target];
    if (force || blend.enabled != current.enabled) {
        if (blend.enabled) {
            m_gl.Enablei(GL_BLEND, target);
        } else {
            m_gl.Disablei(GL_BLEND, target);
        }
    }
    if (force || blend.srcColor != current.srcColor || blend.dstColor != current.dstColor ||
        blend.srcAlpha != current.srcAlpha || blend.dstAlpha != current.dstAlpha) {
        m_gl.BlendFuncSeparatei(target, blend.srcColor, blend.dstColor, blend.srcAlpha, blend.dstAlpha);
    }
    if (force || blend.colorOp != current.colorOp || blend.alphaOp != current.alphaOp) {
        m_gl.BlendEquationSeparatei(target, blend.colorOp, blend.alphaOp);
    }
    if (force || blend.writeMask != current.writeMask) {
        m_gl.ColorMaski(target, blend.writeMask[0], blend.writeMask[1], blend.writeMask[2],
                        blend.writeMask[3]);
    }
}

} // namespace teserra::rhi::opengl
