#pragma once

// What the renderer draws in a frame: plain arrays that the scene layer fills and the renderer reads
// (docs/architecture/renderer.md#inputs-render-data-not-entities). The renderer never sees entities.

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/renderer/camera_data.hpp>

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

/// The render data of one frame. Reuse one packet from frame to frame and clear() it: the vectors keep
/// their capacity, so a steady-state frame allocates nothing.
struct FramePacket {
    /// One per camera, so split screen and minimaps need no new data model.
    std::vector<RenderView> views;
    // Next: sprites (#59), tilemap chunks (#89), debug draw (#96).

    void clear() noexcept { views.clear(); }
};

} // namespace teserra::renderer
