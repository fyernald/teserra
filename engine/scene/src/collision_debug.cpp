#include <teserra/scene/collision_debug.hpp>

#include <teserra/renderer/debug_draw.hpp>
#include <teserra/scene/transform.hpp>

#include <algorithm>

namespace teserra::scene {

void collisionDebugSystem(ecs::SystemContext& context) {
    const CollisionDebug& debug = context.resource<const CollisionDebug>();
    if (!debug.enabled) {
        return;
    }
    const CollisionState& state = context.resource<const CollisionState>();
    auto& lines = context.resource<renderer::DebugDrawList>();
    for (const auto& [entity, collider, global] : context.view<const Collider, const GlobalTransform>()) {
        const bool overlapping = std::ranges::any_of(state.overlapping, [&](const auto& pair) {
            return pair.first == entity || pair.second == entity;
        });
        lines.box(colliderBounds(collider, global.translation()),
                  overlapping ? debug.overlapColor : debug.color);
    }
}

} // namespace teserra::scene
