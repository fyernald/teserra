#pragma once

// What the renderer draws in a frame: plain arrays that the scene layer fills and the renderer reads
// (docs/architecture/renderer.md#inputs-render-data-not-entities). The renderer never sees entities.

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/renderer/camera_data.hpp>
#include <teserra/rhi/handles.hpp>

#include <vector>

namespace teserra::renderer {

/// A camera that renders this frame.
struct RenderView {
    CameraData camera{};
    /// What the view's area is cleared to before it is drawn, in linear values.
    Color clearColor{.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F};
    /// Views render in ascending order; views with the same order keep the order of the packet.
    i32 order = 0;
};

/// How a sprite's color combines with what is behind it. It stands in for the material until materials
/// exist (#97); it is part of the batch id like a material instance will be.
enum class SpriteBlend : u8 {
    /// Straight alpha: color * alpha + destination * (1 - alpha).
    Alpha,
    /// Color already multiplied by alpha: color + destination * (1 - alpha).
    Premultiplied,
    /// Color * alpha added to the destination: glows, particles.
    Additive,
};

/// The part of a texture a sprite shows, in UVs with the origin at the texture's top-left corner (ADR-0024).
struct UvRect {
    Vec2 min{0.0F};
    Vec2 max{1.0F};
};

/// One sprite to draw this frame.
struct SpriteDrawItem {
    /// A full 3D transform: 2D games use the z = 0 plane and a rotation about Z.
    Mat4 world{1.0F};
    UvRect uv{};
    /// World units.
    Vec2 size{1.0F};
    /// The point of the image at the world transform's origin, normalized like UVs: {0, 0} is the image's
    /// top-left corner, {1, 1} its bottom-right one.
    Vec2 pivot{0.5F};
    /// Multiplies the texture, in linear values.
    Color tint{.r = 1.0F, .g = 1.0F, .b = 1.0F, .a = 1.0F};
    rhi::TextureHandle texture{};
    SpriteBlend blend = SpriteBlend::Alpha;
    /// Higher layers draw over lower ones.
    i16 layer = 0;
    /// Orders sprites within a layer: higher values draw later, on top. Top-down games use -y.
    f32 sortDepth = 0.0F;
};

/// The render data of one frame. Reuse one packet from frame to frame and clear() it: the vectors keep
/// their capacity, so a steady-state frame allocates nothing.
struct FramePacket {
    /// One per camera, so split screen and minimaps need no new data model.
    std::vector<RenderView> views;
    /// Drawn in the order of spriteSortKey() (<teserra/renderer/sprite_batcher.hpp>).
    std::vector<SpriteDrawItem> sprites;
    // Next: tilemap chunks (#89), debug draw (#96).

    void clear() noexcept {
        views.clear();
        sprites.clear();
    }
};

} // namespace teserra::renderer
