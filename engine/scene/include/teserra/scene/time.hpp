#pragma once

// The Time resource of every scene (docs/architecture/ecs.md#scenes-own-worlds).

#include <teserra/core/types.hpp>

namespace teserra::scene {

/// Frame timing as a scene sees it; a resource of every Scene's World. The App advances the active scene's
/// Time once per frame, so a scene that is not active keeps its own play time.
///
///     transform.position.x += speed * world.resource<scene::Time>().deltaSeconds();
class Time {
public:
    /// Seconds since the previous frame.
    [[nodiscard]] f32 deltaSeconds() const noexcept { return m_deltaSeconds; }
    /// Seconds of one fixed step, for code that runs in fixed steps.
    [[nodiscard]] f32 fixedStepSeconds() const noexcept { return m_fixedStepSeconds; }
    /// Seconds this scene was active for, up to and including this frame.
    [[nodiscard]] f64 elapsedSeconds() const noexcept { return m_elapsedSeconds; }
    /// Frames this scene was active for, including this one.
    [[nodiscard]] u64 frame() const noexcept { return m_frame; }

    /// Starts a frame of `deltaSeconds`. Called by the App for the active scene, once per frame.
    void advance(f32 deltaSeconds, f32 fixedStepSeconds) noexcept {
        m_deltaSeconds = deltaSeconds;
        m_fixedStepSeconds = fixedStepSeconds;
        m_elapsedSeconds += deltaSeconds;
        ++m_frame;
    }

private:
    f32 m_deltaSeconds = 0.0F;
    f32 m_fixedStepSeconds = 0.0F;
    f64 m_elapsedSeconds = 0.0;
    u64 m_frame = 0;
};

} // namespace teserra::scene
