#include <teserra/scene/hierarchy.hpp>

#include <teserra/core/assert.hpp>

#include <algorithm>

namespace teserra::scene {

namespace {

void detachFromChildren(ecs::World& world, ecs::Entity parent, ecs::Entity child) {
    if (Children* children = world.tryGet<Children>(parent)) {
        std::erase(children->entities, child);
    }
}

} // namespace

bool isDescendantOf(ecs::World& world, ecs::Entity entity, ecs::Entity ancestor) {
    const Parent* link = world.tryGet<Parent>(entity);
    for (u32 depth = 0; link != nullptr && depth < kMaxHierarchyDepth; ++depth) {
        if (link->entity == ancestor) {
            return true;
        }
        link = world.tryGet<Parent>(link->entity);
    }
    return false;
}

void setParent(ecs::World& world, ecs::Entity child, ecs::Entity parent) {
    TESERRA_ASSERT(world.isAlive(child) && world.isAlive(parent),
                   "setParent({}, {}) of an entity that is not alive", child, parent);
    TESERRA_ASSERT(child != parent && !isDescendantOf(world, parent, child),
                   "setParent({}, {}) would make a cycle: {} is {} or one of its descendants", child, parent,
                   parent, child);
    if (Parent* current = world.tryGet<Parent>(child)) {
        if (current->entity == parent) {
            return;
        }
        detachFromChildren(world, current->entity, child);
        current->entity = parent;
    } else {
        world.add<Parent>(child, Parent{.entity = parent});
    }
    if (Children* children = world.tryGet<Children>(parent)) {
        children->entities.push_back(child);
    } else {
        world.add<Children>(parent, Children{.entities = {child}});
    }
}

void removeParent(ecs::World& world, ecs::Entity child) {
    const Parent* current = world.tryGet<Parent>(child);
    if (current == nullptr) {
        return;
    }
    detachFromChildren(world, current->entity, child);
    world.remove<Parent>(child);
}

} // namespace teserra::scene
