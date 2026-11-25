#pragma once

// What a system sees while it runs, checked against what it declared
// (docs/architecture/ecs.md#systems-and-scheduling).

#include <teserra/core/assert.hpp>
#include <teserra/core/types.hpp>
#include <teserra/ecs/commands.hpp>
#include <teserra/ecs/events.hpp>
#include <teserra/ecs/type_key.hpp>
#include <teserra/ecs/view.hpp>
#include <teserra/ecs/world.hpp>

#include <algorithm>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace teserra::ecs {

/// The data a system declared it uses: sorted TypeKeys of components and resources it reads and writes. A
/// write implies a read. In debug builds SystemContext asserts on anything else; a parallel executor will
/// use the same declarations to decide which systems can run together.
struct SystemAccess {
    std::vector<TypeKey> componentReads;
    std::vector<TypeKey> componentWrites;
    std::vector<TypeKey> resourceReads;
    std::vector<TypeKey> resourceWrites;

    /// Whether component `key` may be read (a write implies a read).
    [[nodiscard]] bool readsComponent(TypeKey key) const noexcept {
        return contains(componentReads, key) || contains(componentWrites, key);
    }
    /// Whether component `key` may be written.
    [[nodiscard]] bool writesComponent(TypeKey key) const noexcept { return contains(componentWrites, key); }
    [[nodiscard]] bool readsResource(TypeKey key) const noexcept {
        return contains(resourceReads, key) || contains(resourceWrites, key);
    }
    /// Whether resource `key` may be written.
    [[nodiscard]] bool writesResource(TypeKey key) const noexcept { return contains(resourceWrites, key); }

    /// Adds `key` to the sorted `keys` unless it is there.
    static void insert(std::vector<TypeKey>& keys, TypeKey key) {
        const auto position = std::ranges::lower_bound(keys, key);
        if (position == keys.end() || *position != key) {
            keys.insert(position, key);
        }
    }

private:
    [[nodiscard]] static bool contains(const std::vector<TypeKey>& keys, TypeKey key) noexcept {
        return std::ranges::binary_search(keys, key);
    }
};

/// The event cursors of one system: where it is in each event type it reads.
class EventCursors {
public:
    /// The cursor of event type `type`, created at 0 on first use.
    [[nodiscard]] EventCursor& of(TypeKey type) {
        const auto found =
            std::ranges::find_if(m_cursors, [&](const auto& entry) { return entry.first == type; });
        if (found != m_cursors.end()) {
            return found->second;
        }
        return m_cursors.emplace_back(type, EventCursor{}).second;
    }

private:
    std::vector<std::pair<TypeKey, EventCursor>> m_cursors;
};

/// The world as one system sees it while the Schedule runs it. Views and resources are checked against the
/// system's declarations in debug builds; structural changes go through commands(), applied after the
/// phase.
///
///     void movePlayer(ecs::SystemContext& ctx) {
///         const f32 dt = ctx.resource<const scene::Time>().deltaSeconds();
///         for (auto [entity, transform, player] : ctx.view<scene::Transform, const Player>()) { ... }
///     }
class SystemContext {
public:
    /// `cursors` holds the system's event cursors; without one, readEvents() asserts.
    SystemContext(World& world, Commands& commands, std::string_view systemName, const SystemAccess& access,
                  EventCursors* cursors = nullptr) noexcept
        : m_world(&world),
          m_commands(&commands),
          m_systemName(systemName),
          m_access(&access),
          m_cursors(cursors) {}

    /// world.view<Included...>(exclude<Excluded...>). A const included type needs reads<T>() or writes<T>(),
    /// a mutable one writes<T>(), an excluded one reads<T>() (asserted).
    template <ViewComponent... Included, Component... Excluded>
    [[nodiscard]] View<TypeList<Included...>, TypeList<Excluded...>>
    view(Exclude<Excluded...> excluded = {}) {
        (requireComponent<Included>(), ...);
        (requireComponent<const Excluded>(), ...);
        return m_world->view<Included...>(excluded);
    }

    /// The entity's T, or nullptr if it has none or is not alive: random access next to views, such as a
    /// child reading its parent. tryGet<const T>() needs reads<T>() or writes<T>(), tryGet<T>() needs
    /// writes<T>() (asserted).
    template <typename T>
        requires Component<std::remove_const_t<T>> && (!std::is_empty_v<std::remove_const_t<T>>)
    [[nodiscard]] T* tryGet(Entity entity) {
        requireComponent<T>();
        return m_world->tryGet<std::remove_const_t<T>>(entity);
    }

    /// Whether `entity` has a T, tags included (false if it is not alive): random access for components that
    /// tryGet() cannot return, such as a tag on the other entity of a collision. Needs reads<T>() or
    /// writes<T>() (asserted).
    template <typename T>
        requires Component<std::remove_const_t<T>>
    [[nodiscard]] bool has(Entity entity) const {
        requireComponent<const std::remove_const_t<T>>();
        return m_world->isAlive(entity) && m_world->has<std::remove_const_t<T>>(entity);
    }

    /// Whether `entity` is alive. Needs no declaration.
    [[nodiscard]] bool isAlive(Entity entity) const noexcept { return m_world->isAlive(entity); }

    /// The world's resource T. resource<const T>() needs readsResource<T>() or writesResource<T>(),
    /// resource<T>() needs writesResource<T>() (asserted). The resource must exist (asserted).
    template <typename T>
        requires Resource<std::remove_const_t<T>>
    [[nodiscard]] T& resource() {
        requireResource<T>();
        return m_world->resource<std::remove_const_t<T>>();
    }

    /// Like resource<T>(), or nullptr if the world has no T.
    template <typename T>
        requires Resource<std::remove_const_t<T>>
    [[nodiscard]] T* tryResource() {
        requireResource<T>();
        return m_world->tryResource<std::remove_const_t<T>>();
    }

    /// Sends an event T (Events<T> must exist: addEvents<T>()). Needs sendsEvents<T>() (asserted).
    template <typename T>
    void sendEvent(T event) {
        resource<Events<T>>().send(std::move(event));
    }

    /// Calls f(const T&) for each event T this system has not read yet, oldest first: events sent since its
    /// last run, in this frame or the previous one. Every system has its own cursor, so two readers each see
    /// every event once. Needs readsEvents<T>() (asserted). Returns how many it read.
    template <typename T, typename F>
    usize readEvents(F&& f) {
        const Events<T>& events = resource<const Events<T>>();
        TESERRA_ASSERT(m_cursors != nullptr, "System '{}' has no event cursors to read {} with", m_systemName,
                       componentName<T>());
        return events.read(m_cursors->of(typeKey<Events<T>>()), std::forward<F>(f));
    }

    /// Structural changes, applied at the end of the phase (the sync point).
    [[nodiscard]] Commands& commands() noexcept { return *m_commands; }

    [[nodiscard]] std::string_view systemName() const noexcept { return m_systemName; }

private:
    template <typename T>
    void requireComponent() const {
        using Type = std::remove_const_t<T>;
        if constexpr (std::is_const_v<T>) {
            TESERRA_ASSERT(m_access->readsComponent(typeKey<Type>()),
                           "System '{}' reads {} without declaring it (.reads<{}>())", m_systemName,
                           componentName<Type>(), componentName<Type>());
        } else {
            TESERRA_ASSERT(m_access->writesComponent(typeKey<Type>()),
                           "System '{}' writes {} without declaring it (.writes<{}>())", m_systemName,
                           componentName<Type>(), componentName<Type>());
        }
    }

    template <typename T>
    void requireResource() const {
        using Type = std::remove_const_t<T>;
        if constexpr (std::is_const_v<T>) {
            TESERRA_ASSERT(m_access->readsResource(typeKey<Type>()),
                           "System '{}' reads resource {} without declaring it (.readsResource<{}>())",
                           m_systemName, componentName<Type>(), componentName<Type>());
        } else {
            TESERRA_ASSERT(m_access->writesResource(typeKey<Type>()),
                           "System '{}' writes resource {} without declaring it (.writesResource<{}>())",
                           m_systemName, componentName<Type>(), componentName<Type>());
        }
    }

    World* m_world;
    Commands* m_commands;
    std::string_view m_systemName;
    const SystemAccess* m_access;
    EventCursors* m_cursors;
};

} // namespace teserra::ecs
