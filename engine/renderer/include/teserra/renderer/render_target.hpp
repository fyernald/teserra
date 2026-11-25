#pragma once

// Render targets (docs/architecture/renderer.md#render-targets): the offscreen textures views render into.
// The renderer owns the main scene target, which passes draw into and the composite pass scales to the
// frame's output, and the targets games create for cameras (minimaps, portals, editor viewports).

#include <teserra/core/handle.hpp>
#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/rhi/format.hpp>

#include <string_view>

namespace teserra::renderer {

/// A render target of a Renderer (Renderer::createRenderTarget()). The null handle is the main scene target.
using RenderTargetHandle = Handle<struct RenderTargetTag>;

/// Creation parameters of a render target for cameras.
struct RenderTargetDesc {
    /// Pixels; both above 0.
    UVec2 size{0U};
    /// A color format the RHI can render into and sample, such as Rgba8Srgb.
    rhi::Format colorFormat = rhi::Format::Rgba8Srgb;
    /// Format::Undefined for a target without depth.
    rhi::Format depthFormat = rhi::Format::Depth32Float;
    /// Shown by graphics debuggers; the textures add " color" and " depth".
    std::string_view debugName{};
};

/// How the main scene target is sized and presented (RendererConfig::scene).
struct SceneTargetConfig {
    /// A fixed resolution in pixels, such as {320, 180} for pixel art: the composite pass scales it to the
    /// output by the largest integer factor that fits and fills the rest with `letterboxColor`. {0, 0}
    /// matches the output's size, so the scene is copied one to one.
    UVec2 resolution{0U};
    /// What the composite pass fills around the scaled scene, in linear values.
    Color letterboxColor{.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F};
};

/// Where the composite pass puts the scene target in the output, in output pixels from the top-left corner.
struct CompositeRect {
    Vec2 position{0.0F};
    Vec2 size{0.0F};
    /// Output pixels per scene pixel: a whole number of at least 1, or below 1 when the output is smaller
    /// than the scene (it is then scaled down to fit, keeping its aspect ratio).
    f32 scale = 1.0F;

    friend bool operator==(const CompositeRect&, const CompositeRect&) = default;
};

/// The size of the main scene target for an output of `outputSize` pixels.
[[nodiscard]] UVec2 sceneTargetSize(const SceneTargetConfig& config, UVec2 outputSize) noexcept;

/// Where a scene of `sceneSize` pixels shows in an output of `outputSize` pixels: scaled by the largest
/// integer factor that fits, centered on whole pixels. An output smaller than the scene scales it down to fit
/// instead. Zero sizes give an empty rectangle.
[[nodiscard]] CompositeRect compositeRect(UVec2 sceneSize, UVec2 outputSize) noexcept;

/// The scene pixel under output pixel `outputPixel` (both from the top-left corner), for mouse input on a
/// letterboxed scene. Points in the letterbox map outside [0, scene size).
[[nodiscard]] Vec2 outputToScene(const CompositeRect& rect, Vec2 outputPixel) noexcept;

} // namespace teserra::renderer
