#pragma once

// Component type identity without RTTI (docs/architecture/ecs.md#component-types-and-metadata): a TypeKey
// is the 64-bit FNV-1a hash of a stable type name, so it is the same in every build and across shared
// libraries, unlike static counters or typeid.

#include <teserra/core/types.hpp>

#include <cstddef>
#include <functional>
#include <source_location>
#include <string_view>

namespace teserra::ecs {

/// Identifies a component type: the hash of its name.
struct TypeKey {
    u64 value = 0;

    friend constexpr bool operator==(TypeKey, TypeKey) noexcept = default;
    friend constexpr auto operator<=>(TypeKey, TypeKey) noexcept = default;
};

/// The key of the type named `name`: FNV-1a 64-bit of its bytes.
[[nodiscard]] constexpr TypeKey typeKey(std::string_view name) noexcept {
    u64 hash = 0xcbf2'9ce4'8422'2325ULL;
    for (const char character : name) {
        hash ^= static_cast<u8>(character);
        hash *= 0x0000'0100'0000'01b3ULL;
    }
    return TypeKey{hash};
}

/// The stable name of a component type. Specialize it with TESERRA_COMPONENT(Type, "Name"); types without
/// one use a compiler-derived name.
template <typename T>
struct ComponentName {};

namespace detail {

template <typename T>
concept NamedComponent = requires {
    { ComponentName<T>::kName } -> std::convertible_to<std::string_view>;
};

// The type's name as the compiler spells it in this function's signature: "auto __cdecl
// teserra::ecs::detail::rawTypeName<struct Foo>(void) noexcept" (MSVC), "... rawTypeName() [with T = Foo]"
// (GCC), "... rawTypeName() [T = Foo]" (Clang).
template <typename T>
[[nodiscard]] consteval std::string_view rawTypeName() noexcept {
    const std::string_view signature = std::source_location::current().function_name();
    if (const auto open = signature.find("rawTypeName<"); open != std::string_view::npos) {
        const auto start = open + std::string_view("rawTypeName<").size();
        return signature.substr(start, signature.rfind(">(") - start);
    }
    const auto equals = signature.rfind("T = ");
    const auto start = equals + std::string_view("T = ").size();
    const auto end = signature.find_first_of(";]", start);
    return signature.substr(start, end - start);
}

// Without MSVC's "struct ", "class " and "enum " prefixes, so that compilers agree on plain types.
[[nodiscard]] consteval std::string_view withoutKeyword(std::string_view name) noexcept {
    for (const std::string_view keyword : {"struct ", "class ", "enum "}) {
        if (name.starts_with(keyword)) {
            return name.substr(keyword.size());
        }
    }
    return name;
}

} // namespace detail

/// The component's stable name: the registered one, or the compiler's spelling of the type (namespaces
/// included). Register the name of every component that is saved or sent anywhere: compilers may spell
/// templates differently.
template <typename T>
[[nodiscard]] constexpr std::string_view componentName() noexcept {
    if constexpr (detail::NamedComponent<T>) {
        return ComponentName<T>::kName;
    } else {
        return detail::withoutKeyword(detail::rawTypeName<T>());
    }
}

/// The key of a component type.
template <typename T>
[[nodiscard]] constexpr TypeKey typeKey() noexcept {
    return typeKey(componentName<T>());
}

} // namespace teserra::ecs

template <>
struct std::hash<teserra::ecs::TypeKey> {
    [[nodiscard]] std::size_t operator()(teserra::ecs::TypeKey key) const noexcept {
        return static_cast<std::size_t>(key.value); // already a hash
    }
};

/// Gives the component type `Type` the stable name `Name`, at global scope:
///
///     TESERRA_COMPONENT(game::Velocity, "Velocity")
#define TESERRA_COMPONENT(Type, Name)                                                                        \
    template <>                                                                                              \
    struct teserra::ecs::ComponentName<Type> {                                                               \
        static constexpr std::string_view kName = Name;                                                      \
    }
