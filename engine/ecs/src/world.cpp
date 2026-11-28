#include <teserra/ecs/world.hpp>

#include <teserra/core/assert.hpp>

#include <vector>

namespace teserra::ecs {

Entity World::create() {
    TESERRA_ASSERT(m_iterating == 0, "World::create() during forEachEntity(); use Commands to defer it");
    return m_entities.create();
}

void World::destroy(Entity entity) {
    TESERRA_ASSERT(m_iterating == 0, "World::destroy() during forEachEntity(); use Commands to defer it");
    TESERRA_ASSERT(isAlive(entity), "Destroying entity {}, which is not alive", entity);
    if (!isAlive(entity)) {
        return;
    }
    for (const PoolEntry& entry : m_pools) {
        entry.pool->removeIfPresent(entity);
    }
    m_entities.destroy(entity);
}

void World::destroyAll() {
    TESERRA_ASSERT(m_iterating == 0, "World::destroyAll() during forEachEntity(); use Commands to defer it");
    std::vector<Entity> live;
    live.reserve(entityCount());
    m_entities.forEach([&](Entity entity, const EntityRecord& record) {
        if (!record.pending) {
            live.push_back(entity);
        }
    });
    for (const Entity entity : live) {
        destroy(entity);
    }
}

Entity World::reserve() {
    ++m_pendingCount;
    return m_entities.create(EntityRecord{.pending = true});
}

void World::commitReserved(Entity entity) {
    TESERRA_ASSERT(isReserved(entity), "Entity {} is not reserved", entity);
    if (EntityRecord* record = m_entities.tryGet(entity); record != nullptr && record->pending) {
        record->pending = false;
        --m_pendingCount;
    }
}

void World::releaseReserved(Entity entity) {
    TESERRA_ASSERT(isReserved(entity), "Entity {} is not reserved", entity);
    if (isReserved(entity)) {
        --m_pendingCount;
        m_entities.destroy(entity);
    }
}

} // namespace teserra::ecs

namespace {
[[maybe_unused]] int Scratch_Value = 0; // scratch PR for #142, never merged
}
