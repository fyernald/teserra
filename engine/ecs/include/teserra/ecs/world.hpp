#pragma once

// The World: owner of entities and, from #66 on, of their components (docs/architecture/ecs.md#concepts).

#include <teserra/core/handle_pool.hpp>
#include <teserra/core/types.hpp>
#include <teserra/ecs/entity.hpp>

namespace teserra::ecs {

/// A set of entities and their components. Several worlds can coexist (tests, editor previews); each hands
/// out its own entities. Not thread-safe.
///
///     ecs::World world;
///     const ecs::Entity player = world.create();
///     world.isAlive(player);   // true
///     world.destroy(player);   // player is stale from now on
class World {
public:
    World() = default;
    ~World() = default;

    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) noexcept = default;
    World& operator=(World&&) noexcept = default;

    /// A new live entity. Indices of destroyed entities are reused in the order they were freed, with a new
    /// generation, so stale entities stay detectable for as long as possible.
    [[nodiscard]] Entity create();
    /// Destroys a live entity; it is stale from now on. Destroying a null or stale entity is a programmer
    /// error (asserted).
    void destroy(Entity entity);
    /// Whether `entity` was created by this world and not destroyed since. Never asserts.
    [[nodiscard]] bool isAlive(Entity entity) const noexcept { return m_entities.contains(entity); }
    /// The number of live entities.
    [[nodiscard]] usize entityCount() const noexcept { return m_entities.size(); }

    /// Calls `function(entity)` for every live entity, in index order. The function must not create or
    /// destroy entities (asserted); structural changes during iteration go through Commands (iteration 10).
    template <typename Function>
    void forEachEntity(Function&& function) const {
        const IterationScope scope(m_iterating);
        m_entities.forEach([&](Entity entity, const Alive& /*alive*/) { function(entity); });
    }

private:
    // The registry stores nothing per entity yet; the pool provides the generations and the free list.
    struct Alive {};

    // Counts nested forEachEntity() calls, also when the function throws (test assert handlers do).
    class IterationScope {
    public:
        explicit IterationScope(u32& depth) noexcept
            : m_depth(depth) {
            ++m_depth;
        }
        ~IterationScope() { --m_depth; }
        IterationScope(const IterationScope&) = delete;
        IterationScope& operator=(const IterationScope&) = delete;
        IterationScope(IterationScope&&) = delete;
        IterationScope& operator=(IterationScope&&) = delete;

    private:
        u32& m_depth;
    };

    HandlePool<Alive, EntityTag> m_entities;
    mutable u32 m_iterating = 0;
};

} // namespace teserra::ecs
