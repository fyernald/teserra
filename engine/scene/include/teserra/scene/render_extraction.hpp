#pragma once

// Render extraction: what the scene's cameras see, copied from the ECS into the renderer's FramePacket in the
// Extract phase (docs/architecture/renderer.md#inputs-render-data-not-entities).

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/ecs/system_context.hpp>
#include <teserra/renderer/camera_data.hpp>
#include <teserra/renderer/chunk_mesh_cache.hpp>
#include <teserra/renderer/frame_packet.hpp>
#include <teserra/renderer/renderer.hpp>
#include <teserra/scene/render_components.hpp>
#include <teserra/scene/tilemap.hpp>

#include <string_view>
#include <vector>

namespace teserra::scene {

/// The render data of a scene's frame; a resource of every scene's World. The App sets targetSize before
/// Extract and renders `packet` after it. The vectors keep their capacity, so a steady-state frame allocates
/// nothing.
struct RenderExtraction {
    /// Pixels of the main scene target, which cameras without a target of their own render into, set by the
    /// App every frame. While it is zero (no device, minimized window), nothing is extracted.
    UVec2 targetSize{0U};
    /// Where the render targets of cameras (Camera::target) come from, set by the App: their sizes give the
    /// views' aspect ratios. Cameras with a target are left out without it, or when their target is gone.
    const renderer::Renderer* renderer = nullptr;
    /// The frame's views (one per Camera with a GlobalTransform) and visible sprites, refilled by every
    /// Extract. Views with the same Camera::order render in an unspecified order.
    renderer::FramePacket packet;
    /// The world-space bounds of each view in `packet.views`, in the same order: what sprites are culled
    /// against.
    std::vector<Aabb> viewBounds;
    /// Visible sprites left out this frame because they were outside every view.
    usize culledSprites = 0;

    /// Where tilemap chunks are built, set by the App (its renderer's device); without one, tilemaps are not
    /// extracted.
    renderer::ChunkMeshCache* chunkMeshes = nullptr;
    /// Tilemap chunks left out this frame because they were outside every view.
    usize culledChunks = 0;
    /// Tilemap chunks built (new, changed or evicted) this frame.
    usize builtChunks = 0;
    /// The vertices of the chunk being built; reused.
    std::vector<renderer::SpriteVertex> chunkVertices;
};

/// The name every scene's Schedule gives renderExtractionSystem(), for before()/after() constraints.
inline constexpr std::string_view kRenderExtractionSystemName = "teserra.render_extraction";

/// The extraction system, which every Scene runs in Extract. It clears the RenderExtraction resource, adds a
/// view for every entity with a Camera and a GlobalTransform, and a SpriteDrawItem for every visible
/// SpriteRenderer with a GlobalTransform whose bounds (spriteBounds()) intersect a view's bounds
/// (viewBounds()). With a chunk mesh cache, it also adds a TilemapChunkDrawItem for every chunk with tiles of
/// a visible TilemapRenderer's Tilemap whose bounds (chunkBounds()) intersect a view, building the chunk's
/// mesh when it is dirty, missing from the cache, or placed or drawn differently than when it was built,
/// and then clearing its dirty flag. Reads Camera, GlobalTransform, SpriteRenderer and TilemapRenderer and
/// the Time resource; writes Tilemap (dirty flags) and the RenderExtraction resource. The lines of the
/// world's renderer::DebugDrawList resource go into the packet's debugLines, and the list is cleared.
void renderExtractionSystem(ecs::SystemContext& context);

/// The world-space box around everything a view can see: the corners of its frustum (or box, for an
/// orthographic camera), from the camera's inverseViewProjection.
[[nodiscard]] Aabb viewBounds(const renderer::CameraData& camera) noexcept;

/// The world-space box around the quad of `sprite` drawn at `world`.
[[nodiscard]] Aabb spriteBounds(const SpriteRenderer& sprite, const Mat4& world) noexcept;

/// The world-space box around chunk `chunkCoord` of a tilemap with tiles of `tileSize` placed at `world`.
[[nodiscard]] Aabb chunkBounds(IVec2 chunkCoord, Vec2 tileSize, const Mat4& world) noexcept;

} // namespace teserra::scene
