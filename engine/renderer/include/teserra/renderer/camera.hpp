#pragma once

// Cameras (docs/architecture/renderer.md#cameras): orthographic and perspective projections, the CameraData
// of a view, conversions between screen and world, and Camera2D for 2D games. Conventions are ADR-0024's:
// right-handed world, +Y up, cameras look down -Z, depth [0, 1], screen and viewport origin at the top left.

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/renderer/camera_data.hpp>
#include <teserra/renderer/frame_packet.hpp>

#include <variant>

namespace teserra::renderer {

// The planes are nearPlane / farPlane, not near / far: <windows.h> defines `near` and `far` as macros.

/// A box projection: `height` world units are visible vertically, the width follows the aspect ratio, and
/// the box is centered on the camera.
struct Orthographic {
    f32 height = 10.0F;
    /// Distances in front of the camera; negative values reach behind it, which suits 2D layers at any z.
    f32 nearPlane = -100.0F;
    f32 farPlane = 100.0F;
};

/// A pinhole projection.
struct Perspective {
    /// The vertical field of view; the default is 60 degrees.
    f32 fovYRadians = 1.0472F;
    /// Positive distances in front of the camera, nearPlane < farPlane.
    f32 nearPlane = 0.1F;
    f32 farPlane = 1000.0F;
};

/// How a view sees the world. Where the camera is comes from its world transform, given separately.
struct Camera {
    std::variant<Orthographic, Perspective> projection = Orthographic{};
    /// The part of the target the view covers, normalized: {0, 0} is the top-left corner, {1, 1} the size.
    Rect viewport{.position = Vec2{0.0F}, .size = Vec2{1.0F}};
    /// Views render in ascending order.
    i32 order = 0;
    Color clearColor{.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F};
    /// What the camera renders into: a target of the renderer (Renderer::createRenderTarget()), such as a
    /// minimap that sprites then show, or the null handle for the main scene target.
    RenderTargetHandle target{};
};

/// The projection matrix of `camera` for a viewport of the given width / height.
[[nodiscard]] Mat4 projectionMatrix(const Camera& camera, f32 aspect) noexcept;

/// The camera block of a view: the view matrix is the inverse of `world`, the camera's world transform; the
/// projection uses the aspect ratio of the camera's viewport on a target of `targetSize` pixels.
[[nodiscard]] CameraData cameraData(const Camera& camera, const Mat4& world, UVec2 targetSize,
                                    f32 time = 0.0F) noexcept;

/// A view for the FramePacket: cameraData() with the camera's clear color, order, viewport and target.
/// `targetSize` is the size of the camera's target.
[[nodiscard]] RenderView renderView(const Camera& camera, const Mat4& world, UVec2 targetSize,
                                    f32 time = 0.0F) noexcept;

/// Where the world point `point` appears: x and y in pixels of the view's viewport, from its top-left
/// corner; z the depth in [0, 1] for points between the planes.
[[nodiscard]] Vec3 worldToScreen(const CameraData& camera, const Vec3& point) noexcept;

/// The world point at pixel `pixel` of the view's viewport and depth `depth` (0 on the near plane, 1 on the
/// far one): the inverse of worldToScreen().
[[nodiscard]] Vec3 screenToWorld(const CameraData& camera, const Vec2& pixel, f32 depth) noexcept;

/// A 2D camera on the z = 0 plane, with pan, zoom around a point and pixel snapping. It makes an
/// orthographic Camera whose scale keeps texels square: a sprite drawn at `pixelsPerUnit` texels per world
/// unit shows each texel as `zoom` screen pixels.
///
///     camera2D.zoomAt(1.25F, mouse, viewport);   // zoom in around the cursor
///     packet.views.push_back(renderView(camera2D.camera(viewport), camera2D.world(), viewport));
struct Camera2D {
    /// The world point at the center of the view.
    Vec2 position{0.0F};
    /// Screen pixels per texel; above 0.
    f32 zoom = 1.0F;
    /// Texels per world unit of the game's art.
    f32 pixelsPerUnit = 16.0F;
    /// Rounds the position to whole screen pixels, so pixel art does not shimmer while the camera moves.
    bool pixelSnap = false;

    /// Screen pixels per world unit.
    [[nodiscard]] f32 pixelsPerWorldUnit() const noexcept { return pixelsPerUnit * zoom; }
    /// Moves the camera by `worldDelta`.
    void pan(const Vec2& worldDelta) noexcept { position += worldDelta; }
    /// Multiplies the zoom by `factor` and keeps the world point under `screenPixel` where it is. Zoom stays
    /// above 0.
    void zoomAt(f32 factor, const Vec2& screenPixel, UVec2 viewportSize) noexcept;

    /// The orthographic camera that shows `viewportSize` pixels at this zoom.
    [[nodiscard]] Camera camera(UVec2 viewportSize) const noexcept;
    /// The camera's world transform: a translation to the (snapped) position.
    [[nodiscard]] Mat4 world() const noexcept;
    /// The position the view uses: snapped to whole screen pixels with pixelSnap.
    [[nodiscard]] Vec2 viewPosition() const noexcept;

    [[nodiscard]] Vec2 worldToScreen(const Vec2& point, UVec2 viewportSize) const noexcept;
    [[nodiscard]] Vec2 screenToWorld(const Vec2& pixel, UVec2 viewportSize) const noexcept;
};

} // namespace teserra::renderer
