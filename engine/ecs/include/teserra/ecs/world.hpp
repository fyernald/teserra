#pragma once

// The World: owner of entities and their components (docs/architecture/ecs.md#concepts).

#include <teserra/core/assert.hpp>
#include <teserra/core/handle_pool.hpp>
#include <teserra/core/types.hpp>
#include <teserra/ecs/component_meta.hpp>
#include <teserra/ecs/component_pool.hpp>
#include <teserra/ecs/entity.hpp>
#include <teserra/ecs/type_key.hpp>
#include <teserra/ecs/view.hpp>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace teserra::ecs {

class Commands;

/// A set of entities and their components. Several worlds can coexist (tests, editor previews); each hands
/// out its own entities. Not thread-safe.
///
///     ecs::World world;
///     const ecs::Entity player = world.create();
///     world.add<Velocity>(player, Vec3{1.0F, 0.0F, 0.0F});
///     world.add<Enemy>(player);                    // a tag: no data
///     world.get<Velocity>(player).value.x += 1.0F;
///     world.destroy(player);                       // removes its components; player is stale from now on
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
    /// Destroys a live entity and its components; it is stale from now on. Destroying a null or stale entity
    /// is a programmer error (asserted).
    void destroy(Entity entity);
    /// Whether `entity` was created by this world and not destroyed since. Never asserts.
    [[nodiscard]] bool isAlive(Entity entity) const noexcept {
        const EntityRecord* record = m_entities.tryGet(entity);
        return record != nullptr && !record->pending;
    }
    /// The number of live entities.
    [[nodiscard]] usize entityCount() const noexcept { return m_entities.size() - m_pendingCount; }

    /// Calls `function(entity)` for every live entity, in index order. The function must not create or
    /// destroy entities (asserted); structural changes during iteration go through Commands.
    template <typename Function>
    void forEachEntity(Function&& function) const {
        const IterationScope scope(m_iterating);
        m_entities.forEach([&](Entity entity, const EntityRecord& record) {
            if (!record.pending) {
                function(entity);
            }
        });
    }

    // Components. The entity must be alive, and it must have (get, remove) or lack (add) the component; both
    // are asserted. References stay valid until the next add or remove of that component type.

    /// Gives `entity` a T constructed from `args` and returns it.
    template <Component T, typename... Args>
        requires(!std::is_empty_v<T>)
    T& add(Entity entity, Args&&... args) {
        ComponentPool<T>& target = pool<T>();
        requireAbsent(entity, target);
        return target.add(entity, std::forward<Args>(args)...);
    }
    /// Gives `entity` the tag T.
    template <Component T>
        requires std::is_empty_v<T>
    void add(Entity entity) {
        ComponentPool<T>& target = pool<T>();
        requireAbsent(entity, target);
        target.add(entity);
    }
    template <Component T>
    void remove(Entity entity) {
        ComponentPool<T>* target = tryPool<T>();
        TESERRA_ASSERT(isAlive(entity) && target != nullptr && target->has(entity),
                       "Removing {} from entity {}, which does not have it", componentName<T>(), entity);
        if (target != nullptr && target->has(entity)) {
            target->remove(entity);
        }
    }
    template <Component T>
    [[nodiscard]] bool has(Entity entity) const {
        const ComponentPool<T>* found = tryPool<T>();
        return found != nullptr && found->has(entity);
    }
    template <Component T>
        requires(!std::is_empty_v<T>)
    [[nodiscard]] T& get(Entity entity) {
        T* component = tryGet<T>(entity);
        TESERRA_ASSERT(component != nullptr, "Entity {} has no {}", entity, componentName<T>());
        return *component;
    }
    /// The entity's T, or nullptr if it has none (or is not alive).
    template <Component T>
        requires(!std::is_empty_v<T>)
    [[nodiscard]] T* tryGet(Entity entity) {
        ComponentPool<T>* found = tryPool<T>();
        return found != nullptr ? found->tryGet(entity) : nullptr;
    }

    /// The pool of T, created on first use. Views keep pointers to pools, so iteration never looks them up.
    template <Component T>
    [[nodiscard]] ComponentPool<T>& pool() {
        if (ComponentPool<T>* found = tryPool<T>()) {
            return *found;
        }
        auto created = std::make_unique<ComponentPool<T>>();
        ComponentPool<T>& result = *created;
        const TypeKey key = componentMeta<T>().key;
        m_pools.insert(std::ranges::lower_bound(m_pools, key, {}, &PoolEntry::key),
                       PoolEntry{.key = key, .pool = std::move(created)});
        return result;
    }
    /// The pool of T, or nullptr if no entity of this world ever had a T.
    template <Component T>
    [[nodiscard]] ComponentPool<T>* tryPool() {
        return static_cast<ComponentPool<T>*>(findPool(componentMeta<T>()));
    }
    template <Component T>
    [[nodiscard]] const ComponentPool<T>* tryPool() const {
        return static_cast<const ComponentPool<T>*>(findPool(componentMeta<T>()));
    }
    /// The entities with all of `Included` and none of `Excluded` (docs/architecture/ecs.md#views):
    ///
    ///     for (auto [entity, transform, velocity] : world.view<Transform, const Velocity>(exclude<Frozen>))
    ///
    /// const types are read-only. Creates the pools of types this world has not seen yet, so that the view
    /// never looks them up while it iterates.
    template <ViewComponent... Included, Component... Excluded>
    [[nodiscard]] View<TypeList<Included...>, TypeList<Excluded...>>
    view(Exclude<Excluded...> /*exclude*/ = {}) {
        return {std::tuple{&pool<std::remove_const_t<Included>>()...}, std::tuple{&pool<Excluded>()...}};
    }

    /// The number of component pools, one per component type this world has seen.
    [[nodiscard]] usize poolCount() const noexcept { return m_pools.size(); }

private:
    friend class Commands;

    // The pool provides the generations and the free list. A pending entity was reserved by Commands and is
    // not alive until the commands are applied.
    struct EntityRecord {
        bool pending = false;
    };

    // Commands (docs/architecture/ecs.md#structural-changes-commands). reserve() returns the entity create()
    // would have, from the same free list, but pending. It is allowed during forEachEntity(): HandlePool
    // walks its slots by index, and the callback above holds no reference across the user's function.
    [[nodiscard]] Entity reserve();
    // Makes a reserved entity alive.
    void commitReserved(Entity entity);
    // Releases a reserved entity that was never committed; copies of it become stale.
    void releaseReserved(Entity entity);
    // Whether `entity` is a reserved entity that is not alive yet.
    [[nodiscard]] bool isReserved(Entity entity) const noexcept {
        const EntityRecord* record = m_entities.tryGet(entity);
        return record != nullptr && record->pending;
    }

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

    // The pool of the type `meta` describes, or nullptr. Not noexcept: it asserts on a TypeKey collision, and
    // test assert handlers throw.
    [[nodiscard]] ComponentPoolBase* findPool(const ComponentMeta& meta) const {
        const auto found = std::ranges::lower_bound(m_pools, meta.key, {}, &PoolEntry::key);
        if (found == m_pools.end() || found->key != meta.key) {
            return nullptr;
        }
        // A key names one type: two types whose names hash alike cannot share a world.
        TESERRA_ASSERT(&found->pool->meta() == &meta,
                       "Component types {} and {} have the same TypeKey; register a different name for one",
                       found->pool->meta().name, meta.name);
        return found->pool.get();
    }

    void requireAbsent([[maybe_unused]] Entity entity,
                       [[maybe_unused]] const ComponentPoolBase& target) const {
        TESERRA_ASSERT(isAlive(entity), "Adding {} to entity {}, which is not alive", target.meta().name,
                       entity);
        TESERRA_ASSERT(!target.has(entity), "Entity {} already has {}", entity, target.meta().name);
    }

    HandlePool<EntityRecord, EntityTag> m_entities;
    usize m_pendingCount = 0;
    mutable u32 m_iterating = 0;
    // Sorted by key: a world has a few dozen component types, views cache their pools, and a vector moves
    // without allocating, unlike a hash map.
    struct PoolEntry {
        TypeKey key;
        std::unique_ptr<ComponentPoolBase> pool;
    };
    std::vector<PoolEntry> m_pools;
};

} // namespace teserra::ecs
