#include <teserra/renderer/camera.hpp>

#include <algorithm>
#include <cmath>
#include <variant>

namespace teserra::renderer {

namespace {

// The smallest zoom zoomAt() leaves, so that a view always shows a finite area.
constexpr f32 kMinZoom = 1.0e-4F;

Vec2 viewportPixels(const Camera& camera, UVec2 targetSize) noexcept {
    return camera.viewport.size * Vec2(static_cast<f32>(targetSize.x), static_cast<f32>(targetSize.y));
}

} // namespace

Mat4 projectionMatrix(const Camera& camera, f32 aspect) noexcept {
    // get_if rather than std::visit, which may throw and so cannot sit in a noexcept function.
    if (const auto* lens = std::get_if<Perspective>(&camera.projection)) {
        return math::perspective(lens->fovYRadians, aspect, lens->nearPlane, lens->farPlane);
    }
    const auto* box = std::get_if<Orthographic>(&camera.projection);
    const Orthographic fallback{}; // only for a valueless variant, which a Camera never holds
    const Orthographic& ortho = box != nullptr ? *box : fallback;
    const f32 halfHeight = ortho.height * 0.5F;
    const f32 halfWidth = halfHeight * aspect;
    return math::orthographic(-halfWidth, halfWidth, -halfHeight, halfHeight, ortho.nearPlane,
                              ortho.farPlane);
}

CameraData cameraData(const Camera& camera, const Mat4& world, UVec2 targetSize, f32 time) noexcept {
    const Vec2 pixels = viewportPixels(camera, targetSize);
    const f32 aspect = pixels.y > 0.0F ? pixels.x / pixels.y : 1.0F; // a minimized window has no area
    CameraData data;
    data.view = math::inverse(world);
    data.projection = projectionMatrix(camera, aspect);
    data.viewProjection = data.projection * data.view;
    data.inverseViewProjection = math::inverse(data.viewProjection);
    data.position = Vec4(Vec3(world[3]), 1.0F);
    data.viewportSize = pixels;
    data.time = time;
    return data;
}

RenderView renderView(const Camera& camera, const Mat4& world, UVec2 targetSize, f32 time) noexcept {
    return {.camera = cameraData(camera, world, targetSize, time),
            .clearColor = camera.clearColor,
            .order = camera.order,
            .viewport = camera.viewport};
}

Vec3 worldToScreen(const CameraData& camera, const Vec3& point) noexcept {
    const Vec4 clip = camera.viewProjection * Vec4(point, 1.0F);
    const Vec3 ndc = Vec3(clip) / clip.w;
    // NDC +Y is up; viewport pixels count rows from the top (ADR-0024).
    return {(ndc.x + 1.0F) * 0.5F * camera.viewportSize.x, (1.0F - ndc.y) * 0.5F * camera.viewportSize.y,
            ndc.z};
}

Vec3 screenToWorld(const CameraData& camera, const Vec2& pixel, f32 depth) noexcept {
    const Vec2 ndc{(pixel.x / camera.viewportSize.x * 2.0F) - 1.0F,
                   1.0F - (pixel.y / camera.viewportSize.y * 2.0F)};
    const Vec4 world = camera.inverseViewProjection * Vec4(ndc, depth, 1.0F);
    return Vec3(world) / world.w;
}

void Camera2D::zoomAt(f32 factor, const Vec2& screenPixel, UVec2 viewportSize) noexcept {
    const Vec2 before = screenToWorld(screenPixel, viewportSize);
    zoom = std::max(zoom * factor, kMinZoom);
    position += before - screenToWorld(screenPixel, viewportSize);
}

Camera Camera2D::camera(UVec2 viewportSize) const noexcept {
    return {.projection = Orthographic{.height = static_cast<f32>(viewportSize.y) / pixelsPerWorldUnit()}};
}

Vec2 Camera2D::viewPosition() const noexcept {
    if (!pixelSnap) {
        return position;
    }
    const f32 scale = pixelsPerWorldUnit();
    return {std::round(position.x * scale) / scale, std::round(position.y * scale) / scale};
}

Mat4 Camera2D::world() const noexcept {
    Mat4 world{1.0F};
    world[3] = Vec4(viewPosition(), 0.0F, 1.0F);
    return world;
}

Vec2 Camera2D::worldToScreen(const Vec2& point, UVec2 viewportSize) const noexcept {
    const Vec2 offset = (point - viewPosition()) * pixelsPerWorldUnit();
    const Vec2 center = Vec2(static_cast<f32>(viewportSize.x), static_cast<f32>(viewportSize.y)) * 0.5F;
    return {center.x + offset.x, center.y - offset.y}; // screen rows count down
}

Vec2 Camera2D::screenToWorld(const Vec2& pixel, UVec2 viewportSize) const noexcept {
    const Vec2 center = Vec2(static_cast<f32>(viewportSize.x), static_cast<f32>(viewportSize.y)) * 0.5F;
    const Vec2 offset{pixel.x - center.x, center.y - pixel.y};
    return viewPosition() + (offset / pixelsPerWorldUnit());
}

} // namespace teserra::renderer
