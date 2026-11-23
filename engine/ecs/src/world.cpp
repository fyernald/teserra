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

} // namespace teserra::ecs
