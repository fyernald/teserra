#pragma once

// Debug draw (docs/architecture/renderer.md#debug-draw): immediate-mode lines, rectangles, circles, crosses
// and boxes in 3D world space, drawn over the frame by the debug pass and gone the frame after.
//
//   auto& debug = ctx.resource<renderer::DebugDrawList>();   // .writesResource<renderer::DebugDrawList>()
//   debug.line(a, b, Color{1, 0, 0, 1});
//   debug.circle(player.position, 0.5F, kGreen);
//
// Compiled out with TESERRA_WITH_DEBUG_DRAW=OFF: every call is then an empty inline function.

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <vector>

#ifndef TESERRA_WITH_DEBUG_DRAW
#define TESERRA_WITH_DEBUG_DRAW 1
#endif

namespace teserra::renderer {

/// Whether debug draw is compiled in (CMake option TESERRA_WITH_DEBUG_DRAW).
inline constexpr bool kDebugDrawCompiled = TESERRA_WITH_DEBUG_DRAW != 0;

/// A vertex of a debug line: a world position and a linear color as RGBA bytes.
struct DebugVertex {
    Vec3 position{0.0F};
    std::array<u8, 4> color{};
};
static_assert(sizeof(DebugVertex) == 16);

/// Lines to draw this frame, two vertices each. The list keeps its capacity when it is cleared, so a
/// steady-state frame allocates nothing. Shapes are outlines in world space.
class DebugDrawList {
public:
    /// While disabled, shapes are ignored. Enabled by default.
    void setEnabled(bool enabled) noexcept { m_enabled = enabled; }
    [[nodiscard]] bool isEnabled() const noexcept { return kDebugDrawCompiled && m_enabled; }

    void line(const Vec3& a, const Vec3& b, const Color& color) {
        if constexpr (kDebugDrawCompiled) {
            if (m_enabled) {
                const std::array<u8, 4> bytes = toBytes(color);
                m_vertices.push_back({.position = a, .color = bytes});
                m_vertices.push_back({.position = b, .color = bytes});
            }
        }
    }

    /// The outline of the rectangle from `rect.position` to `rect.position + rect.size` on x and y (world
    /// units, +Y up), at height `z`.
    void rect(const Rect& rect, const Color& color, f32 z = 0.0F) {
        const Vec3 a{rect.position, z};
        const Vec3 b{rect.position.x + rect.size.x, rect.position.y, z};
        const Vec3 c{rect.position + rect.size, z};
        const Vec3 d{rect.position.x, rect.position.y + rect.size.y, z};
        line(a, b, color);
        line(b, c, color);
        line(c, d, color);
        line(d, a, color);
    }

    /// A circle of `segments` lines around `center` in the plane z = center.z.
    void circle(const Vec3& center, f32 radius, const Color& color, u32 segments = 32) {
        if (!isEnabled() || segments < 3) {
            return;
        }
        const f32 step = 2.0F * std::numbers::pi_v<f32> / static_cast<f32>(segments);
        Vec3 previous = center + Vec3{radius, 0.0F, 0.0F};
        for (u32 i = 1; i <= segments; ++i) {
            const f32 angle = step * static_cast<f32>(i);
            const Vec3 next = center + Vec3{std::cos(angle) * radius, std::sin(angle) * radius, 0.0F};
            line(previous, next, color);
            previous = next;
        }
    }

    /// Three lines of length `size` through `point`, along x, y and z.
    void cross(const Vec3& point, f32 size, const Color& color) {
        const f32 half = size * 0.5F;
        line(point - Vec3{half, 0.0F, 0.0F}, point + Vec3{half, 0.0F, 0.0F}, color);
        line(point - Vec3{0.0F, half, 0.0F}, point + Vec3{0.0F, half, 0.0F}, color);
        line(point - Vec3{0.0F, 0.0F, half}, point + Vec3{0.0F, 0.0F, half}, color);
    }

    /// The twelve edges of `box`; a 2D box (min.z == max.z) draws its rectangle twice over.
    void box(const Aabb& box, const Color& color) {
        const Vec3& lo = box.min;
        const Vec3& hi = box.max;
        for (const f32 z : {lo.z, hi.z}) {
            rect({.position = {lo.x, lo.y}, .size = {hi.x - lo.x, hi.y - lo.y}}, color, z);
        }
        for (const Vec2 corner : {Vec2{lo.x, lo.y}, Vec2{hi.x, lo.y}, Vec2{hi.x, hi.y}, Vec2{lo.x, hi.y}}) {
            line({corner, lo.z}, {corner, hi.z}, color);
        }
    }

    /// The vertices of the lines so far, two per line.
    [[nodiscard]] std::span<const DebugVertex> vertices() const noexcept { return m_vertices; }
    [[nodiscard]] usize lineCount() const noexcept { return m_vertices.size() / 2; }

    /// Removes every line, keeping the memory.
    void clear() noexcept { m_vertices.clear(); }

private:
    static u8 toByte(f32 value) noexcept {
        return static_cast<u8>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
    }
    static std::array<u8, 4> toBytes(const Color& color) noexcept {
        return {toByte(color.r), toByte(color.g), toByte(color.b), toByte(color.a)};
    }

    std::vector<DebugVertex> m_vertices;
    bool m_enabled = true;
};

} // namespace teserra::renderer
