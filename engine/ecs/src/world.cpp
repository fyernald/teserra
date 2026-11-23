#include <teserra/ecs/world.hpp>

#include <teserra/core/assert.hpp>

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
