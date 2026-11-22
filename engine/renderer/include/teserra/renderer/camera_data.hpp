#pragma once

// The camera block of every engine shader, at set 0 (rhi::kFrameGroup), binding 0
// (docs/architecture/renderer.md#cameras).

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>

#include <cstddef>

namespace teserra::renderer {

/// One view's camera as shaders see it: the CPU side of the std140 uniform block
///
///     layout(set = 0, binding = 0) uniform Camera {
///         mat4 view; mat4 projection; mat4 viewProjection; mat4 inverseViewProjection;
///         vec4 position; vec2 viewportSize; float time;
///     } camera;
///
/// Matrices are column-major and follow ADR-0024 (right-handed, +Y up, depth [0, 1]).
struct CameraData {
    Mat4 view{1.0F};
    Mat4 projection{1.0F};
    Mat4 viewProjection{1.0F};
    Mat4 inverseViewProjection{1.0F};
    /// The camera's world position in xyz; w is 1.
    Vec4 position{0.0F, 0.0F, 0.0F, 1.0F};
    /// The size of the view's viewport in pixels.
    Vec2 viewportSize{0.0F};
    /// Seconds of game time, for animated shaders; set by whoever fills the view.
    f32 time = 0.0F;
    f32 padding = 0.0F; // std140 rounds the block up to 16 bytes
};

// The std140 layout of the shader block: a change here needs the same change in every shader.
static_assert(sizeof(CameraData) == 288);
static_assert(offsetof(CameraData, projection) == 64);
static_assert(offsetof(CameraData, viewProjection) == 128);
static_assert(offsetof(CameraData, inverseViewProjection) == 192);
static_assert(offsetof(CameraData, position) == 256);
static_assert(offsetof(CameraData, viewportSize) == 272);
static_assert(offsetof(CameraData, time) == 280);

} // namespace teserra::renderer
