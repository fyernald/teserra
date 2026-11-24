#pragma once

// Collision debug drawing (docs/architecture/ecs.md#collision): the outlines of every Collider, drawn into
// the scene's renderer::DebugDrawList while the CollisionDebug resource is enabled.

#include <teserra/core/math.hpp>
#include <teserra/ecs/system_context.hpp>
#include <teserra/scene/collision.hpp>

#include <string_view>

namespace teserra::scene {

/// Whether collider outlines are drawn, and in which colors: a resource of every scene. Off by default.
struct CollisionDebug {
    bool enabled = false;
    /// Colliders that overlap nothing.
    Color color{.r = 0.2F, .g = 1.0F, .b = 0.4F, .a = 1.0F};
    /// Colliders in an overlapping pair.
    Color overlapColor{.r = 1.0F, .g = 0.3F, .b = 0.2F, .a = 1.0F};
};

/// The name every scene's Schedule gives collisionDebugSystem(), in PostUpdate after the collisions.
inline constexpr std::string_view kCollisionDebugSystemName = "teserra.collision_debug";

/// Draws the box of every entity with a Collider and a GlobalTransform while CollisionDebug::enabled.
/// Reads Collider, GlobalTransform and the CollisionDebug and CollisionState resources; writes the
/// renderer::DebugDrawList resource.
void collisionDebugSystem(ecs::SystemContext& context);

} // namespace teserra::scene
