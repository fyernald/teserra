#pragma once

// Engine math (ADR-0008) and conventions (ADR-0024). Code outside engine/core/ uses these names, never
// glm:: directly, so glm stays an implementation detail that could be replaced.
//
// Conventions: right-handed, +Y up, cameras look down -Z; column vectors (v' = M * v); radians; clip-space
// depth [0, 1]; positive rotation is counter-clockwise. All types are 3D-capable; 2D content uses the
// z = 0 plane, and a 2D box is an Aabb with zero thickness.
//
// glm is configured once, by PUBLIC compile definitions on teserra_core (GLM_FORCE_DEPTH_ZERO_TO_ONE,
// GLM_FORCE_RADIANS). Nothing else may define GLM_* macros: mixed configurations break the ODR.

#include <teserra/core/types.hpp>

#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <numbers>

#if !defined(GLM_FORCE_DEPTH_ZERO_TO_ONE) || !defined(GLM_FORCE_RADIANS)
#error                                                                                                       \
    "Teserra math needs the glm configuration of teserra_core: link Teserra::Core instead of adding glm yourself"
#endif

namespace teserra {

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using IVec2 = glm::ivec2;
using UVec2 = glm::uvec2;
using Mat3 = glm::mat3;
using Mat4 = glm::mat4;
using Quat = glm::quat;

/// Linear RGBA color, components normally in [0, 1]. sRGB conversion is the renderer's job. Laid out as
/// four floats, so it can be copied into GPU-visible structs.
struct Color {
    f32 r = 0.0F;
    f32 g = 0.0F;
    f32 b = 0.0F;
    f32 a = 1.0F;

    friend constexpr bool operator==(const Color&, const Color&) = default;
};
static_assert(sizeof(Color) == 4 * sizeof(f32), "Color must stay four tightly packed floats");

/// Axis-aligned rectangle in 2D pixel or texture space: `position` is the top-left corner (ADR-0024: top-
/// left origin for render targets, viewports and UVs), `size` extends right and down.
struct Rect {
    Vec2 position{0.0F};
    Vec2 size{0.0F};

    friend bool operator==(const Rect&, const Rect&) = default;
};

/// Axis-aligned bounding box in world space, inclusive of its faces. A 2D box is an Aabb with
/// min.z == max.z. Valid when min <= max on every axis.
struct Aabb {
    Vec3 min{0.0F};
    Vec3 max{0.0F};

    friend bool operator==(const Aabb&, const Aabb&) = default;
};

namespace math {

// Vectors

[[nodiscard]] inline f32 dot(const Vec2& a, const Vec2& b) noexcept {
    return glm::dot(a, b);
}
[[nodiscard]] inline f32 dot(const Vec3& a, const Vec3& b) noexcept {
    return glm::dot(a, b);
}
[[nodiscard]] inline f32 dot(const Vec4& a, const Vec4& b) noexcept {
    return glm::dot(a, b);
}

/// Right-handed: cross(+X, +Y) == +Z.
[[nodiscard]] inline Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return glm::cross(a, b);
}

[[nodiscard]] inline f32 length(const Vec2& v) noexcept {
    return glm::length(v);
}
[[nodiscard]] inline f32 length(const Vec3& v) noexcept {
    return glm::length(v);
}

/// Unit vector in the direction of `v`, which must not be zero (the result is NaN otherwise).
[[nodiscard]] inline Vec2 normalize(const Vec2& v) noexcept {
    return glm::normalize(v);
}
[[nodiscard]] inline Vec3 normalize(const Vec3& v) noexcept {
    return glm::normalize(v);
}
[[nodiscard]] inline Vec4 normalize(const Vec4& v) noexcept {
    return glm::normalize(v);
}
[[nodiscard]] inline Quat normalize(const Quat& q) noexcept {
    return glm::normalize(q);
}

/// a + (b - a) * t for scalars and vectors; t is not clamped.
template <typename T>
[[nodiscard]] inline T lerp(const T& a, const T& b, f32 t) noexcept {
    return a + (b - a) * t;
}

[[nodiscard]] inline Color lerp(const Color& a, const Color& b, f32 t) noexcept {
    return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t), lerp(a.a, b.a, t)};
}

[[nodiscard]] constexpr f32 radians(f32 degrees) noexcept {
    return degrees * (std::numbers::pi_v<f32> / 180.0F);
}

// Matrices and rotations

// The clip planes are zNear/zFar, not near/far: <windows.h> defines `near` and `far` as macros.

/// Right-handed perspective projection looking down -Z, depth [0, 1], +Y up in NDC. `fovY` in
/// radians; zNear and zFar are positive distances with zNear < zFar.
[[nodiscard]] inline Mat4 perspective(f32 fovY, f32 aspect, f32 zNear, f32 zFar) noexcept {
    return glm::perspectiveRH_ZO(fovY, aspect, zNear, zFar);
}

/// Right-handed orthographic projection looking down -Z, depth [0, 1], +Y up in NDC. The box spans
/// [left, right] x [bottom, top] and the distances [zNear, zFar] in front of the camera.
[[nodiscard]] inline Mat4 orthographic(f32 left, f32 right, f32 bottom, f32 top, f32 zNear,
                                       f32 zFar) noexcept {
    return glm::orthoRH_ZO(left, right, bottom, top, zNear, zFar);
}

/// Right-handed view matrix: the camera at `eye` looks at `target`, `up` points roughly up.
[[nodiscard]] inline Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) noexcept {
    return glm::lookAtRH(eye, target, up);
}

/// Inverse of an invertible matrix (the result is undefined for singular matrices).
[[nodiscard]] inline Mat4 inverse(const Mat4& m) noexcept {
    return glm::inverse(m);
}
[[nodiscard]] inline Mat3 inverse(const Mat3& m) noexcept {
    return glm::inverse(m);
}

/// Rotation by `angle` radians around the unit vector `axis`, counter-clockwise when looking against
/// the axis (from +axis toward the origin).
[[nodiscard]] inline Quat angleAxis(f32 angle, const Vec3& axis) noexcept {
    return glm::angleAxis(angle, axis);
}

[[nodiscard]] inline Mat4 toMat4(const Quat& q) noexcept {
    return glm::mat4_cast(q);
}

// Boxes

[[nodiscard]] inline Vec3 center(const Aabb& box) noexcept {
    return (box.min + box.max) * 0.5F;
}

[[nodiscard]] inline Vec3 size(const Aabb& box) noexcept {
    return box.max - box.min;
}

/// True if `point` is inside or on the boundary of `box`.
[[nodiscard]] constexpr bool contains(const Aabb& box, const Vec3& point) noexcept {
    return box.min.x <= point.x && point.x <= box.max.x && box.min.y <= point.y && point.y <= box.max.y &&
           box.min.z <= point.z && point.z <= box.max.z;
}

/// True if the boxes overlap or touch. Works for zero-thickness (2D) boxes in the same plane.
[[nodiscard]] constexpr bool intersects(const Aabb& a, const Aabb& b) noexcept {
    return a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y && b.min.y <= a.max.y &&
           a.min.z <= b.max.z && b.min.z <= a.max.z;
}

/// Smallest box containing both boxes.
[[nodiscard]] inline Aabb merge(const Aabb& a, const Aabb& b) noexcept {
    return {glm::min(a.min, b.min), glm::max(a.max, b.max)};
}

/// True if `point` is inside `rect`; the right and bottom edges are exclusive, like pixel ranges.
[[nodiscard]] constexpr bool contains(const Rect& rect, const Vec2& point) noexcept {
    return rect.position.x <= point.x && point.x < rect.position.x + rect.size.x &&
           rect.position.y <= point.y && point.y < rect.position.y + rect.size.y;
}

} // namespace math

} // namespace teserra
