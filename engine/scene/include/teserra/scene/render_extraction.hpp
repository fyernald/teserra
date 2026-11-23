#pragma once

// Render extraction: what the scene's cameras see, copied from the ECS into the renderer's FramePacket in the
// Extract phase (docs/architecture/renderer.md#inputs-render-data-not-entities).

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/ecs/system_context.hpp>
#include <teserra/renderer/camera_data.hpp>
#include <teserra/renderer/frame_packet.hpp>
#include <teserra/scene/render_components.hpp>

#include <string_view>
#include <vector>

namespace teserra::scene {

/// The render data of a scene's frame; a resource of every scene's World. The App sets targetSize before
/// Extract and renders `packet` after it. The vectors keep their capacity, so a steady-state frame allocates
/// nothing.
struct RenderExtraction {
    /// Pixels of the target the views render into, set by the App every frame. While it is zero (no device,
    /// minimized window), nothing is extracted.
    UVec2 targetSize{0U};
    /// The frame's views (one per Camera with a GlobalTransform) and visible sprites, refilled by every
    /// Extract. Views with the same Camera::order render in an unspecified order.
    renderer::FramePacket packet;
    /// The world-space bounds of each view in `packet.views`, in the same order: what sprites are culled
    /// against.
    std::vector<Aabb> viewBounds;
    /// Visible sprites left out this frame because they were outside every view.
    usize culledSprites = 0;
};

/// The name every scene's Schedule gives renderExtractionSystem(), for before()/after() constraints.
inline constexpr std::string_view kRenderExtractionSystemName = "teserra.render_extraction";

/// The extraction system, which every Scene runs in Extract. It clears the RenderExtraction resource, adds a
/// view for every entity with a Camera and a GlobalTransform, and a SpriteDrawItem for every visible
/// SpriteRenderer with a GlobalTransform whose bounds (spriteBounds()) intersect a view's bounds
/// (viewBounds()). Reads Camera, GlobalTransform and SpriteRenderer and the Time resource; writes the
/// RenderExtraction resource.
void renderExtractionSystem(ecs::SystemContext& context);

/// The world-space box around everything a view can see: the corners of its frustum (or box, for an
/// orthographic camera), from the camera's inverseViewProjection.
[[nodiscard]] Aabb viewBounds(const renderer::CameraData& camera) noexcept;

/// The world-space box around the quad of `sprite` drawn at `world`.
[[nodiscard]] Aabb spriteBounds(const SpriteRenderer& sprite, const Mat4& world) noexcept;

} // namespace teserra::scene
