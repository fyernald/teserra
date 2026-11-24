#include <teserra/scene/collision.hpp>

#include <teserra/scene/transform.hpp>

#include <algorithm>
#include <functional>

namespace teserra::scene {

namespace {

u64 keyOf(ecs::Entity entity) noexcept {
    return (u64{entity.generation()} << 32U) | entity.index();
}

bool pairLess(const std::pair<ecs::Entity, ecs::Entity>& x,
              const std::pair<ecs::Entity, ecs::Entity>& y) noexcept {
    return std::pair(keyOf(x.first), keyOf(x.second)) < std::pair(keyOf(y.first), keyOf(y.second));
}

// Overlap of the insides: boxes that only touch do not overlap.
bool overlaps(const Aabb& a, const Aabb& b) noexcept {
    return a.min.x < b.max.x && b.min.x < a.max.x && a.min.y < b.max.y && b.min.y < a.max.y;
}

} // namespace

void collisionSystem(ecs::SystemContext& context) {
    CollisionState& state = context.resource<CollisionState>();
    state.boxes.clear();
    for (const auto& [entity, collider, global] : context.view<const Collider, const GlobalTransform>()) {
        state.boxes.push_back({.entity = entity, .bounds = colliderBounds(collider, global.translation())});
    }
    // Sort and sweep along x: only boxes whose x ranges overlap are compared.
    std::ranges::sort(state.boxes, {}, [](const CollisionState::Box& box) { return box.bounds.min.x; });
    std::swap(state.previous, state.overlapping);
    state.overlapping.clear();
    for (usize i = 0; i < state.boxes.size(); ++i) {
        for (usize j = i + 1;
             j < state.boxes.size() && state.boxes[j].bounds.min.x < state.boxes[i].bounds.max.x; ++j) {
            if (overlaps(state.boxes[i].bounds, state.boxes[j].bounds)) {
                ecs::Entity a = state.boxes[i].entity;
                ecs::Entity b = state.boxes[j].entity;
                if (keyOf(b) < keyOf(a)) {
                    std::swap(a, b);
                }
                state.overlapping.emplace_back(a, b);
            }
        }
    }
    std::ranges::sort(state.overlapping, pairLess);

    // Both lists are sorted: walk them together.
    auto& started = context.resource<ecs::Events<CollisionStarted>>();
    auto& ended = context.resource<ecs::Events<CollisionEnded>>();
    usize now = 0;
    usize before = 0;
    while (now < state.overlapping.size() || before < state.previous.size()) {
        if (before == state.previous.size() ||
            (now < state.overlapping.size() && pairLess(state.overlapping[now], state.previous[before]))) {
            started.send({.a = state.overlapping[now].first, .b = state.overlapping[now].second});
            ++now;
        } else if (now == state.overlapping.size() ||
                   pairLess(state.previous[before], state.overlapping[now])) {
            ended.send({.a = state.previous[before].first, .b = state.previous[before].second});
            ++before;
        } else {
            ++now;
            ++before;
        }
    }
}

} // namespace teserra::scene
