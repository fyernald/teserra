#pragma once

// What the renderer draws in a frame: plain arrays that the scene layer fills and the renderer reads
// (docs/architecture/renderer.md#inputs-render-data-not-entities). The renderer never sees entities.

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/renderer/camera_data.hpp>
#include <teserra/renderer/debug_draw.hpp>
#include <teserra/renderer/render_target.hpp>
#include <teserra/rhi/handles.hpp>

#include <vector>

namespace teserra::renderer {

class MaterialInstance;

/// A camera that renders this frame.
struct RenderView {
    CameraData camera{};
    /// What the view's area is cleared to before it is drawn, in linear values.
    Color clearColor{.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F};
    /// Views render in ascending order; views with the same order keep the order of the packet.
    i32 order = 0;
    /// The part of the target the view covers, normalized: {0, 0} is the top-left corner, {1, 1} the size.
    Rect viewport{.position = Vec2{0.0F}, .size = Vec2{1.0F}};
    /// What the view renders into: a target of the renderer (Renderer::createRenderTarget()), or the null
    /// handle for the main scene target.
    RenderTargetHandle target{};
};

/// How a sprite of the built-in sprite_unlit material combines with what is behind it; part of the batch id.
/// A sprite with a material uses the material's blend mode instead.
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

    friend bool operator==(const UvRect&, const UvRect&) = default;
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
    /// Draws with this material instead of sprite_unlit (docs/architecture/renderer.md#sprite-materials):
    /// its parameters at set 2, `texture` at set 3. The game keeps the instance alive while sprites use it.
    const MaterialInstance* material = nullptr;
    /// Higher layers draw over lower ones.
    i16 layer = 0;
    /// Orders sprites within a layer: higher values draw later, on top. Top-down games use -y.
    f32 sortDepth = 0.0F;
};

/// One built tilemap chunk to draw this frame (docs/architecture/renderer.md#tilemaps): a static vertex
/// buffer of SpriteVertex quads in world space, from a ChunkMeshCache. It draws with the sprite material,
/// like a batch of sprites, before the sprites of its layer.
struct TilemapChunkDrawItem {
    /// Four vertices per quad, in the corner order of SpriteBatcher::writeVertices().
    rhi::BufferHandle vertexBuffer{};
    u32 quadCount = 0;
    /// The tileset's texture, sampled with the sprite pass's tilemap filter (nearest by default).
    rhi::TextureHandle texture{};
    SpriteBlend blend = SpriteBlend::Alpha;
    /// Chunks draw over sprites of lower layers and under sprites of their own and higher layers.
    i16 layer = 0;
};

/// The render data of one frame. Reuse one packet from frame to frame and clear() it: the vectors keep
/// their capacity, so a steady-state frame allocates nothing.
struct FramePacket {
    /// One per camera, so split screen and minimaps need no new data model.
    std::vector<RenderView> views;
    /// Drawn in the order of spriteSortKey() (<teserra/renderer/sprite_batcher.hpp>).
    std::vector<SpriteDrawItem> sprites;
    /// Drawn by layer; chunks of the same layer keep their order.
    std::vector<TilemapChunkDrawItem> tilemapChunks;
    /// Debug lines in world space, two vertices each (DebugDrawList), drawn over everything by the debug
    /// pass.
    std::vector<DebugVertex> debugLines;

    void clear() noexcept {
        views.clear();
        sprites.clear();
        tilemapChunks.clear();
        debugLines.clear();
    }
};

} // namespace teserra::renderer
