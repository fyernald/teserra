#pragma once

// Transform hierarchies (docs/architecture/ecs.md#transform-hierarchies): a child's GlobalTransform is its
// parent's times its own Transform, so a weapon follows the hand that holds it.
//
//   setParent(world, sword, hand);                  // or ctx.commands().add<Parent>(sword, Parent{hand})
//   world.get<Transform>(sword).position = {0.3F, 0.0F, 0.0F};   // relative to the hand
//
// The transform system propagates top-down every PostUpdate, and destroys the descendants of destroyed
// entities through commands.

#include <teserra/ecs/entity.hpp>
#include <teserra/ecs/type_key.hpp>
#include <teserra/ecs/world.hpp>

#include <vector>

namespace teserra::scene {

/// The entity this one is attached to. Its Transform is relative to the parent's GlobalTransform. Set it
/// with setParent() or as a component (also through commands); changing `entity` reparents.
struct Parent {
    ecs::Entity entity{};
};

/// The entities whose Parent is this one, in the order they were attached. Maintained by setParent(),
/// removeParent() and the transform system; read it, do not write it.
struct Children {
    std::vector<ecs::Entity> entities{};
};

/// Attaches `child` to `parent`: gives the child a Parent (or changes it) and updates the Children of the
/// old and the new parent. Both must be alive, and `parent` must not be `child` or one of its descendants
/// (asserted: hierarchies have no cycles). The child's Transform keeps its values, now relative to `parent`.
void setParent(ecs::World& world, ecs::Entity child, ecs::Entity parent);

/// Detaches `child` from its parent, if it has one: removes its Parent and its entry in the parent's
/// Children. Its Transform is then relative to the world.
void removeParent(ecs::World& world, ecs::Entity child);

/// Whether `ancestor` is `entity`'s parent, its parent's parent, and so on.
[[nodiscard]] bool isDescendantOf(ecs::World& world, ecs::Entity entity, ecs::Entity ancestor);

/// Deepest hierarchy the transform system propagates; a deeper chain means a cycle (asserted).
inline constexpr u32 kMaxHierarchyDepth = 1024;

} // namespace teserra::scene

TESERRA_COMPONENT(teserra::scene::Parent, "Parent");
TESERRA_COMPONENT(teserra::scene::Children, "Children");
