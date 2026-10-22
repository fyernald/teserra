#pragma once

// Graphics pipelines: shaders, vertex layout, every piece of fixed-function state and the formats of the
// render targets, baked into one immutable object (docs/architecture/rhi.md#graphics-pipelines). There are
// no commands that change blend, depth or raster state; a different state is a different pipeline.

#include <teserra/core/flags.hpp>
#include <teserra/core/types.hpp>
#include <teserra/rhi/format.hpp>
#include <teserra/rhi/handles.hpp>
#include <teserra/rhi/limits.hpp>
#include <teserra/rhi/sampler.hpp>

#include <array>
#include <span>
#include <string_view>

namespace teserra::rhi {

/// Whether a vertex buffer advances per vertex or per instance.
enum class VertexStepRate : u8 {
    PerVertex,
    PerInstance,
};

/// One vertex buffer binding of a pipeline; its index is the binding slot used by draws.
struct VertexBufferLayout {
    /// Bytes between consecutive elements.
    u32 stride = 0;
    VertexStepRate stepRate = VertexStepRate::PerVertex;
};

/// One vertex shader input.
struct VertexAttribute {
    /// The shader's `layout(location = N)`.
    u32 location = 0;
    /// Index into GraphicsPipelineDesc::vertexBuffers.
    u32 bufferIndex = 0;
    VertexFormat format = VertexFormat::Float;
    /// Byte offset within an element of the buffer.
    u32 offset = 0;
};

/// How vertices are assembled into primitives.
enum class PrimitiveTopology : u8 {
    TriangleList,
    TriangleStrip,
    LineList,
    LineStrip,
    PointList,
};

/// Which triangle faces are discarded.
enum class CullMode : u8 {
    None,
    Front,
    Back,
};

/// Winding of front-facing triangles, as seen in the RHI's top-left-origin framebuffer.
enum class FrontFace : u8 {
    CounterClockwise,
    Clockwise,
};

/// Whether triangles are filled or drawn as outlines (debug views).
enum class FillMode : u8 {
    Fill,
    Line,
};

/// Depth offset applied to rasterized fragments, e.g. against shadow acne.
struct DepthBias {
    f32 constantFactor = 0.0F;
    f32 slopeFactor = 0.0F;
    /// Largest absolute bias; 0 for no clamp.
    f32 clamp = 0.0F;
};

struct RasterState {
    CullMode cullMode = CullMode::None;
    FrontFace frontFace = FrontFace::CounterClockwise;
    FillMode fillMode = FillMode::Fill;
    DepthBias depthBias{};
};

/// Depth test and write. Stencil state is future work and will be added here.
struct DepthStencilState {
    bool depthTest = false;
    bool depthWrite = false;
    /// Passes when it compares the fragment's depth with the stored one as true. Depth is 0 (near) to 1
    /// (far).
    CompareOp depthCompare = CompareOp::Less;
};

/// Multiplier of a source or destination value in blending.
enum class BlendFactor : u8 {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
};

/// How the weighted source and destination are combined.
enum class BlendOp : u8 {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
};

/// Color components a pipeline writes.
enum class ColorWriteMask : u8 {
    None = 0,
    Red = 1U << 0U,
    Green = 1U << 1U,
    Blue = 1U << 2U,
    Alpha = 1U << 3U,
    All = Red | Green | Blue | Alpha,
};
TESERRA_FLAGS(ColorWriteMask)

/// Blending of one color target. Disabled blending writes the source color.
struct BlendState {
    bool enabled = false;
    BlendFactor srcColor = BlendFactor::One;
    BlendFactor dstColor = BlendFactor::Zero;
    BlendOp colorOp = BlendOp::Add;
    BlendFactor srcAlpha = BlendFactor::One;
    BlendFactor dstAlpha = BlendFactor::Zero;
    BlendOp alphaOp = BlendOp::Add;
    ColorWriteMask writeMask = ColorWriteMask::All;
};

/// Formats of the render targets a pipeline draws into; a render pass using the pipeline must match.
struct RenderTargetLayout {
    /// One per color attachment; Format::Undefined for unused slots. Used slots come first.
    std::array<Format, kMaxColorAttachments> colorFormats{};
    /// Format::Undefined without a depth-stencil attachment.
    Format depthStencilFormat = Format::Undefined;
    u32 sampleCount = 1;
};

/// Creation parameters of a graphics pipeline.
struct GraphicsPipelineDesc {
    ShaderModuleHandle vertexShader{};
    ShaderModuleHandle fragmentShader{};
    /// Bind group layouts the shaders use (iteration 6); null for shaders without resources.
    PipelineLayoutHandle layout{};
    /// Indexed by VertexAttribute::bufferIndex and by the draw's vertex buffer slot. Only read during
    /// creation, like vertexAttributes.
    std::span<const VertexBufferLayout> vertexBuffers{};
    std::span<const VertexAttribute> vertexAttributes{};
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    RasterState raster{};
    DepthStencilState depthStencil{};
    /// Indexed like targets.colorFormats. A C array, like RenderPassDesc::colorAttachments, so that
    /// `.colorBlend = {{.enabled = true, ...}}` needs no third pair of braces.
    BlendState colorBlend[kMaxColorAttachments]{}; // NOLINT(modernize-avoid-c-arrays)
    RenderTargetLayout targets{};
    /// Shown by graphics debuggers and in validation messages.
    std::string_view debugName{};
};

} // namespace teserra::rhi
