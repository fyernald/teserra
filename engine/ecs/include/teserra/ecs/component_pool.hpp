#pragma once

// Component pools (docs/architecture/ecs.md#component-storage, ADR-0019): one sparse set per component
// type. A paged sparse array maps entity indices to dense indices; dense arrays hold the entities and their
// components packed in the same order. Add and remove are O(1) (swap-and-pop), so iteration order is
// unspecified and changes after removals.

#include <teserra/core/assert.hpp>
#include <teserra/core/types.hpp>
#include <teserra/ecs/component_meta.hpp>
#include <teserra/ecs/entity.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace teserra::ecs {

/// The part of a pool that does not depend on the component type, for the world's type-erased work.
class ComponentPoolBase {
public:
    virtual ~ComponentPoolBase() = default;

    ComponentPoolBase(const ComponentPoolBase&) = delete;
    ComponentPoolBase& operator=(const ComponentPoolBase&) = delete;
    ComponentPoolBase(ComponentPoolBase&&) = delete;
    ComponentPoolBase& operator=(ComponentPoolBase&&) = delete;

    [[nodiscard]] const ComponentMeta& meta() const noexcept { return *m_meta; }
    /// The number of entities that have the component.
    [[nodiscard]] usize size() const noexcept { return m_entities.size(); }
    /// The entities that have the component, in dense order; components() follows the same order.
    [[nodiscard]] std::span<const Entity> entities() const noexcept { return m_entities; }

    /// Whether `entity`, with its generation, has the component: a stale entity never does.
    [[nodiscard]] bool has(Entity entity) const noexcept {
        const u32 index = denseIndex(entity.index());
        return index != kAbsent && m_entities[index] == entity;
    }
    /// Removes the entity's component if it has one; for World::destroy().
    void removeIfPresent(Entity entity) {
        if (has(entity)) {
            removeAt(entity);
        }
    }

    /// Holds the pool against structural changes while it lives: views take one while they iterate.
    class Lock {
    public:
        explicit Lock(const ComponentPoolBase& pool) noexcept
            : m_pool(&pool) {
            ++pool.m_locks;
        }
        ~Lock() {
            if (m_pool != nullptr) {
                --m_pool->m_locks;
            }
        }
        Lock(Lock&& other) noexcept
            : m_pool(std::exchange(other.m_pool, nullptr)) {}
        Lock& operator=(Lock&&) = delete;
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;

    private:
        const ComponentPoolBase* m_pool;
    };
    /// Locks the pool: adding or removing components while a lock lives asserts.
    [[nodiscard]] Lock lock() const noexcept { return Lock(*this); }
    [[nodiscard]] bool isLocked() const noexcept { return m_locks > 0; }
    /// Pages of the sparse array allocated so far: one per range of kPageSize entity indices that ever had
    /// the component.
    [[nodiscard]] usize allocatedPages() const noexcept {
        return static_cast<usize>(
            std::ranges::count_if(m_pages, [](const auto& page) { return page != nullptr; }));
    }

protected:
    explicit ComponentPoolBase(const ComponentMeta& meta) noexcept
        : m_meta(&meta) {}

    static constexpr u32 kAbsent = std::numeric_limits<u32>::max();
    /// Entities per page of the sparse array; a page is allocated when an entity in its range first gets
    /// the component.
    static constexpr u32 kPageSize = 4096;

    [[nodiscard]] u32 denseIndex(u32 entityIndex) const noexcept {
        const u32 page = entityIndex / kPageSize;
        if (page >= m_pages.size() || !m_pages[page]) {
            return kAbsent;
        }
        return (*m_pages[page])[entityIndex % kPageSize];
    }

    // Appends `entity` to the dense entities and points its sparse entry at it; returns its dense index.
    u32 insertEntity(Entity entity) {
        requireUnlocked("add");
        const u32 page = entity.index() / kPageSize;
        if (page >= m_pages.size()) {
            m_pages.resize(page + 1);
        }
        if (!m_pages[page]) {
            m_pages[page] = std::make_unique<Page>();
            m_pages[page]->fill(kAbsent);
        }
        const auto dense = static_cast<u32>(m_entities.size());
        (*m_pages[page])[entity.index() % kPageSize] = dense;
        m_entities.push_back(entity);
        return dense;
    }

    // Moves the last dense entity into `entity`'s place and pops it (swap-and-pop); returns the dense index
    // that was freed, which subclasses mirror in their component array.
    u32 eraseEntity(Entity entity) {
        requireUnlocked("remove");
        const u32 index = denseIndex(entity.index());
        const Entity last = m_entities.back();
        m_entities[index] = last;
        sparseEntry(last.index()) = index;
        m_entities.pop_back();
        sparseEntry(entity.index()) = kAbsent;
        return index;
    }

    virtual void removeAt(Entity entity) = 0;

private:
    using Page = std::array<u32, kPageSize>;

    u32& sparseEntry(u32 entityIndex) noexcept {
        return (*m_pages[entityIndex / kPageSize])[entityIndex % kPageSize];
    }

    void requireUnlocked([[maybe_unused]] const char* change) const {
        TESERRA_ASSERT(m_locks == 0, "Cannot {} {} while the pool is iterated; use Commands to defer it",
                       change, m_meta->name);
    }

    const ComponentMeta* m_meta;
    std::vector<std::unique_ptr<Page>> m_pages;
    std::vector<Entity> m_entities;
    mutable u32 m_locks = 0;
};

/// The pool of component type T. Tags (empty types) store no components: only the entities that have them.
template <Component T>
class ComponentPool final : public ComponentPoolBase {
public:
    static constexpr bool kTag = std::is_empty_v<T>;

    ComponentPool() noexcept
        : ComponentPoolBase(componentMeta<T>()) {}

    /// Gives `entity` a component constructed from `args`; the entity must not have one yet (asserted by the
    /// world). Returns the component, valid until the next add or remove on this pool.
    template <typename... Args>
    decltype(auto) add(Entity entity, Args&&... args) {
        insertEntity(entity);
        if constexpr (!kTag) {
            return m_components.emplace_back(std::forward<Args>(args)...);
        }
    }

    /// Removes the entity's component; the entity must have one (asserted by the world).
    void remove(Entity entity) { removeAt(entity); }

    /// The entity's component; it must have one.
    [[nodiscard]] T& get(Entity entity) noexcept
        requires(!kTag)
    {
        return m_components[denseIndex(entity.index())];
    }
    /// The entity's component, or nullptr if it has none.
    [[nodiscard]] T* tryGet(Entity entity) noexcept
        requires(!kTag)
    {
        return has(entity) ? &m_components[denseIndex(entity.index())] : nullptr;
    }
    [[nodiscard]] const T* tryGet(Entity entity) const noexcept
        requires(!kTag)
    {
        return has(entity) ? &m_components[denseIndex(entity.index())] : nullptr;
    }

    /// The components in dense order, matching entities().
    [[nodiscard]] std::span<T> components() noexcept
        requires(!kTag)
    {
        return m_components;
    }
    [[nodiscard]] std::span<const T> components() const noexcept
        requires(!kTag)
    {
        return m_components;
    }

private:
    void removeAt(Entity entity) override {
        const u32 index = eraseEntity(entity);
        if constexpr (!kTag) {
            if (index != m_components.size() - 1) {
                m_components[index] = std::move(m_components.back());
            }
            m_components.pop_back();
        }
    }

    // No vector for tags, so a tag pool allocates nothing but its entities.
    struct NoComponents {};
    std::conditional_t<kTag, NoComponents, std::vector<T>> m_components;
};

} // namespace teserra::ecs
