#pragma once

// Simple collision for 2D games, without a physics engine (docs/architecture/ecs.md#collision): axis-aligned
// boxes that move against solid tiles without tunneling, and overlap events between entities.
//
//   const MoveResult result = moveAgainstTiles(query, ground, [&](TileId t) { return tileset.isSolid(t); },
//                                              colliderBounds(collider, transform.position), velocity * dt);
//   transform.position += Vec3{result.moved, 0.0F};
//   ctx.readEvents<CollisionStarted>([&](const CollisionStarted& hit) { ... });

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/ecs/entity.hpp>
#include <teserra/ecs/system_context.hpp>
#include <teserra/ecs/type_key.hpp>
#include <teserra/scene/tile_queries.hpp>
#include <teserra/scene/tilemap.hpp>

#include <algorithm>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace teserra::scene {

/// An axis-aligned box around an entity, in world units on the XY plane: centered on the entity's world
/// position plus `offset`. Rotation and scale do not change it.
struct Collider {
    Vec2 size{1.0F};
    Vec2 offset{0.0F};
};

/// The world box of `collider` on an entity at `position` (z keeps the position's).
[[nodiscard]] inline Aabb colliderBounds(const Collider& collider, const Vec3& position) noexcept {
    const Vec2 center = Vec2{position.x, position.y} + collider.offset;
    const Vec2 half = collider.size * 0.5F;
    return {.min = {center - half, position.z}, .max = {center + half, position.z}};
}

/// How far a box moved, and whether a solid tile stopped it on an axis.
struct MoveResult {
    Vec2 moved{0.0F};
    bool hitX = false;
    bool hitY = false;
};

/// Moves `box` by `delta` against the tiles of `layer` for which isSolid(TileId) is true: first along x,
/// then along y. Each axis sweeps the whole path, so no speed tunnels through a tile; the box stops flush
/// against the first solid tile ahead. Tiles the box already overlaps do not block it, so a box that starts
/// inside a wall can leave. The map must not be rotated. Does not allocate.
template <typename IsSolid>
[[nodiscard]] MoveResult moveAgainstTiles(const TilemapQuery& query, const TilemapLayer& layer,
                                          IsSolid&& isSolid, Aabb box, Vec2 delta) {
    MoveResult result;
    const auto sweep = [&](int axis, f32 amount) -> f32 {
        if (amount == 0.0F) {
            return 0.0F;
        }
        Aabb swept = box;
        if (amount > 0.0F) {
            swept.max[axis] += amount;
        } else {
            swept.min[axis] += amount;
        }
        f32 allowed = amount;
        query.tilesOverlapping(layer, swept, [&](IVec2 coord, TileId tile) {
            if (!isSolid(tile)) {
                return;
            }
            const Aabb bounds = query.tileBounds(coord);
            if (amount > 0.0F && bounds.min[axis] >= box.max[axis] - 1e-4F) {
                allowed = std::min(allowed, std::max(0.0F, bounds.min[axis] - box.max[axis]));
            } else if (amount < 0.0F && bounds.max[axis] <= box.min[axis] + 1e-4F) {
                allowed = std::max(allowed, std::min(0.0F, bounds.max[axis] - box.min[axis]));
            }
        });
        box.min[axis] += allowed;
        box.max[axis] += allowed;
        return allowed;
    };
    result.moved.x = sweep(0, delta.x);
    result.hitX = result.moved.x != delta.x;
    result.moved.y = sweep(1, delta.y);
    result.hitY = result.moved.y != delta.y;
    return result;
}

/// Sent when the colliders of two entities start to overlap; `a` is the entity with the lower id.
struct CollisionStarted {
    ecs::Entity a{};
    ecs::Entity b{};

    /// The entity `self` collided with; nothing if `self` is neither of the two.
    [[nodiscard]] std::optional<ecs::Entity> other(ecs::Entity self) const noexcept {
        return self == a ? std::optional(b) : self == b ? std::optional(a) : std::nullopt;
    }
};

/// Sent when the colliders of two entities stop overlapping, or one of them lost its collider or died.
struct CollisionEnded {
    ecs::Entity a{};
    ecs::Entity b{};

    /// The entity `self` stopped colliding with; nothing if `self` is neither of the two.
    [[nodiscard]] std::optional<ecs::Entity> other(ecs::Entity self) const noexcept {
        return self == a ? std::optional(b) : self == b ? std::optional(a) : std::nullopt;
    }
};

/// The overlapping pairs of the last collision update, and reused memory: a resource of every scene.
struct CollisionState {
    struct Box {
        ecs::Entity entity{};
        Aabb bounds{};
    };
    std::vector<std::pair<ecs::Entity, ecs::Entity>> overlapping;
    std::vector<std::pair<ecs::Entity, ecs::Entity>> previous;
    std::vector<Box> boxes;
};

/// The name every scene's Schedule gives collisionSystem(), in PostUpdate after the transforms.
inline constexpr std::string_view kCollisionSystemName = "teserra.collisions";

/// Finds the overlapping pairs of entities with a Collider and a GlobalTransform (sort and sweep along x),
/// and sends CollisionStarted for new pairs and CollisionEnded for pairs that ended. Boxes that only touch
/// do not overlap. Reads Collider and GlobalTransform; writes the CollisionState resource; sends both events.
void collisionSystem(ecs::SystemContext& context);

} // namespace teserra::scene

TESERRA_COMPONENT(teserra::scene::Collider, "Collider");
