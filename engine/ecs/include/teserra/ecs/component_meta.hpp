#pragma once

// Component metadata (docs/architecture/ecs.md#component-types-and-metadata): name, size, alignment and
// type-erased lifecycle functions of a component type, plus optional field descriptors. v1 does not consume
// the fields; serialization, the editor inspector and scripting bindings will (ADR-0014).

#include <teserra/core/types.hpp>
#include <teserra/ecs/type_key.hpp>

#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace teserra::ecs {

/// The type of a described field.
enum class FieldType : u8 {
    Bool,
    I32,
    U32,
    F32,
    Vec2,
    Vec3,
    Vec4,
    Quat,
    Color,
    Entity,
};

/// One field of a component: its name, its byte offset in the component and its type.
struct FieldDescriptor {
    std::string_view name{};
    u32 offset = 0;
    FieldType type = FieldType::F32;
};

/// The fields of a component type. Specialize it with a `static constexpr std::array<FieldDescriptor, N>
/// kFields` to describe a type's fields; types without one have none.
template <typename T>
struct ComponentFields {};

/// What the world knows about a component type.
struct ComponentMeta {
    TypeKey key{};
    std::string_view name{};
    u32 size = 0;
    u32 alignment = 0;
    /// An empty type: entities have it or not, and it stores no data.
    bool tag = false;
    /// Move-constructs the object at `to` from the one at `from`.
    void (*moveConstruct)(void* to, void* from) = nullptr;
    /// Destroys the object at `object`.
    void (*destroy)(void* object) = nullptr;
    std::span<const FieldDescriptor> fields{};
};

namespace detail {

template <typename T>
concept DescribedComponent = requires { ComponentFields<T>::kFields; };

template <typename T>
[[nodiscard]] constexpr std::span<const FieldDescriptor> componentFields() noexcept {
    if constexpr (DescribedComponent<T>) {
        return ComponentFields<T>::kFields;
    } else {
        return {};
    }
}

template <typename T>
inline constexpr ComponentMeta kComponentMeta{
    .key = typeKey<T>(),
    .name = componentName<T>(),
    .size = std::is_empty_v<T> ? 0U : static_cast<u32>(sizeof(T)),
    .alignment = static_cast<u32>(alignof(T)),
    .tag = std::is_empty_v<T>,
    .moveConstruct = [](void* to, void* from) { ::new (to) T(std::move(*static_cast<T*>(from))); },
    .destroy = [](void* object) { static_cast<T*>(object)->~T(); },
    .fields = componentFields<T>(),
};

} // namespace detail

/// A type the world can store as a component: no references, no arrays, no cv qualifiers, and movable (pools
/// move components when they remove one).
template <typename T>
concept Component =
    std::is_object_v<T> && !std::is_array_v<T> && std::is_same_v<T, std::remove_cv_t<T>> &&
    std::is_move_constructible_v<T> && std::is_move_assignable_v<T> && std::is_destructible_v<T>;

/// The metadata of component type `T`, one instance per type.
template <Component T>
[[nodiscard]] constexpr const ComponentMeta& componentMeta() noexcept {
    return detail::kComponentMeta<T>;
}

} // namespace teserra::ecs
