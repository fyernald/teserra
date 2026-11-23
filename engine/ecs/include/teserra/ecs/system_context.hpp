#pragma once

// What a system sees while it runs, checked against what it declared
// (docs/architecture/ecs.md#systems-and-scheduling).

#include <teserra/core/assert.hpp>
#include <teserra/core/types.hpp>
#include <teserra/ecs/commands.hpp>
#include <teserra/ecs/type_key.hpp>
#include <teserra/ecs/view.hpp>
#include <teserra/ecs/world.hpp>

#include <algorithm>
#include <string_view>
#include <type_traits>
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

    [[nodiscard]] bool readsComponent(TypeKey key) const noexcept {
        return contains(componentReads, key) || contains(componentWrites, key);
    }
    [[nodiscard]] bool writesComponent(TypeKey key) const noexcept { return contains(componentWrites, key); }
    [[nodiscard]] bool readsResource(TypeKey key) const noexcept {
        return contains(resourceReads, key) || contains(resourceWrites, key);
    }
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
    SystemContext(World& world, Commands& commands, std::string_view systemName,
                  const SystemAccess& access) noexcept
        : m_world(&world), m_commands(&commands), m_systemName(systemName), m_access(&access) {}

    /// world.view<Included...>(exclude<Excluded...>). A const included type needs reads<T>() or writes<T>(),
    /// a mutable one writes<T>(), an excluded one reads<T>() (asserted).
    template <ViewComponent... Included, Component... Excluded>
    [[nodiscard]] View<TypeList<Included...>, TypeList<Excluded...>>
    view(Exclude<Excluded...> excluded = {}) {
        (requireComponent<Included>(), ...);
        (requireComponent<const Excluded>(), ...);
        return m_world->view<Included...>(excluded);
    }

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
};

} // namespace teserra::ecs
