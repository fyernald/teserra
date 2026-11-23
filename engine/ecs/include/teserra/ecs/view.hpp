#pragma once

// Views (docs/architecture/ecs.md#views): iteration over the entities that have a set of components and lack
// others. A view walks the smallest included pool and checks membership in the rest (ADR-0019), allocates
// nothing, and locks its pools against structural changes while it lives.
//
//     for (auto [entity, transform, velocity] : world.view<Transform, const Velocity>(exclude<Frozen>)) {
//         transform.position += velocity.value * dt;   // velocity is const Velocity&
//     }

#include <teserra/core/types.hpp>
#include <teserra/ecs/component_meta.hpp>
#include <teserra/ecs/component_pool.hpp>
#include <teserra/ecs/entity.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace teserra::ecs {

/// A list of types, for View's parameters.
template <typename... T>
struct TypeList {};

/// The component types a view skips: world.view<A>(exclude<Frozen, Dead>).
template <Component... T>
struct Exclude {};
// Lower case like std::in_place_type: it reads as part of the call, world.view<A>(exclude<B>).
template <Component... T>
inline constexpr Exclude<T...> exclude{}; // NOLINT(readability-identifier-naming)

/// A component type a view includes: a component, read-only when const.
template <typename T>
concept ViewComponent = Component<std::remove_const_t<T>>;

template <typename Included, typename Excluded>
class View;

/// The entities with all of `Included` and none of `Excluded`, each as a std::tuple<Entity, Included&...>:
/// const types give const references. A tag in `Included` binds to a shared empty object. Iteration order is
/// the order of the smallest included pool, which is unspecified. Created by World::view(); not copyable,
/// and it locks its pools until it is destroyed, so adding or removing its components meanwhile asserts.
template <ViewComponent... Included, Component... Excluded>
class View<TypeList<Included...>, TypeList<Excluded...>> {
    static_assert(sizeof...(Included) > 0, "a view includes at least one component type");

    template <typename T>
    using PoolOf = ComponentPool<std::remove_const_t<T>>;

public:
    using Element = std::tuple<Entity, Included&...>;

    View(std::tuple<PoolOf<Included>*...> included, std::tuple<ComponentPool<Excluded>*...> excluded) noexcept
        : m_included(included),
          m_excluded(excluded),
          m_leader(smallest(included)),
          m_locks(std::apply([](auto*... pools) { return Locks{ComponentPoolBase::Lock(*pools)...}; },
                             std::tuple_cat(included, excluded))) {}

    View(const View&) = delete;
    View& operator=(const View&) = delete;
    View(View&&) = delete;
    View& operator=(View&&) = delete;
    ~View() = default;

    class Iterator {
    public:
        // The names std::input_iterator requires.
        using difference_type = std::ptrdiff_t; // NOLINT(readability-identifier-naming)
        using value_type = Element;             // NOLINT(readability-identifier-naming)

        Iterator() noexcept = default;
        Iterator(const View* view, usize index) noexcept
            : m_view(view), m_index(index) {
            skipNonMembers();
        }

        [[nodiscard]] Element operator*() const {
            return m_view->element(m_view->m_leader->entities()[m_index]);
        }
        Iterator& operator++() noexcept {
            ++m_index;
            skipNonMembers();
            return *this;
        }
        Iterator operator++(int) noexcept {
            Iterator before = *this;
            ++*this;
            return before;
        }
        friend bool operator==(const Iterator& a, const Iterator& b) noexcept {
            return a.m_index == b.m_index;
        }

    private:
        void skipNonMembers() noexcept {
            const std::span<const Entity> entities = m_view->m_leader->entities();
            while (m_index < entities.size() && !m_view->contains(entities[m_index])) {
                ++m_index;
            }
        }

        const View* m_view = nullptr;
        usize m_index = 0;
    };

    [[nodiscard]] Iterator begin() const noexcept { return Iterator(this, 0); }
    [[nodiscard]] Iterator end() const noexcept { return Iterator(this, m_leader->size()); }
    [[nodiscard]] bool empty() const noexcept { return begin() == end(); }
    /// The size of the pool the view walks: an upper bound of the entities it yields.
    [[nodiscard]] usize leaderSize() const noexcept { return m_leader->size(); }

    /// Calls `function(entity, components...)` for every entity of the view.
    template <typename Function>
    void each(Function&& function) const {
        for (Element element : *this) {
            std::apply(function, element);
        }
    }

private:
    using Locks = std::array<ComponentPoolBase::Lock, sizeof...(Included) + sizeof...(Excluded)>;

    static const ComponentPoolBase* smallest(const std::tuple<PoolOf<Included>*...>& pools) noexcept {
        const std::array<const ComponentPoolBase*, sizeof...(Included)> bases = std::apply(
            [](auto*... pool) { return std::array<const ComponentPoolBase*, sizeof...(Included)>{pool...}; },
            pools);
        return *std::ranges::min_element(bases, {}, &ComponentPoolBase::size);
    }

    [[nodiscard]] bool contains(Entity entity) const noexcept {
        return std::apply([&](auto*... pools) { return (pools->has(entity) && ...); }, m_included) &&
               std::apply([&](auto*... pools) { return (!pools->has(entity) && ...); }, m_excluded);
    }

    template <typename T>
    [[nodiscard]] T& component(Entity entity) const noexcept {
        if constexpr (std::is_empty_v<std::remove_const_t<T>>) {
            static std::remove_const_t<T> tag{}; // tags have no data; every entity shares this one
            return tag;
        } else {
            return std::get<PoolOf<T>*>(m_included)->get(entity);
        }
    }

    [[nodiscard]] Element element(Entity entity) const noexcept {
        return Element{entity, component<Included>(entity)...};
    }

    std::tuple<PoolOf<Included>*...> m_included;
    std::tuple<ComponentPool<Excluded>*...> m_excluded;
    const ComponentPoolBase* m_leader;
    Locks m_locks;
};

} // namespace teserra::ecs
