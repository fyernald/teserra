#pragma once

// Generational handles (ADR-0020): RHI resources, entities and assets are referred to by a Handle<Tag>,
// an index plus a generation, never by pointer or backend ID. Each resource kind has its own tag, so
// handles of different kinds do not convert into each other:
//
//   using BufferHandle = Handle<struct BufferTag>;
//
// Handles are non-owning values. The owner (a HandlePool, a device, the ECS world) decides when the
// object behind a handle is destroyed; afterwards the handle is stale, and lookups detect it.

#include <teserra/core/types.hpp>

#include <compare>
#include <cstddef>
#include <format>
#include <functional>
#include <string_view>

namespace teserra {

/// Reference to an object owned by a pool or registry: a slot index and the generation of the slot when
/// the object was created. The default value is the null handle. The layout is an implementation detail
/// (it may be packed later): use the accessors.
template <typename Tag>
class Handle {
public:
    /// The null handle.
    constexpr Handle() noexcept = default;

    /// For owners (pools, registries) that hand out handles. `generation` 0 makes a null handle.
    constexpr Handle(u32 index, u32 generation) noexcept
        : m_index(index), m_generation(generation) {}

    /// True for the null handle, which never refers to an object.
    [[nodiscard]] constexpr bool isNull() const noexcept { return m_generation == 0; }
    [[nodiscard]] constexpr u32 index() const noexcept { return m_index; }
    [[nodiscard]] constexpr u32 generation() const noexcept { return m_generation; }

    friend constexpr bool operator==(Handle, Handle) noexcept = default;
    /// Orders by index, then generation, so handles can be sorted and used as map keys.
    friend constexpr auto operator<=>(Handle, Handle) noexcept = default;

private:
    u32 m_index = 0;
    u32 m_generation = 0; // 0 is reserved for null; live slots start at 1
};

} // namespace teserra

// Specializing std templates for program-defined types is allowed ([namespace.std]); clang-tidy flags
// partial specializations anyway.
template <typename Tag>
struct std::hash<teserra::Handle<Tag>> { // NOLINT(bugprone-std-namespace-modification)
    [[nodiscard]] std::size_t operator()(teserra::Handle<Tag> handle) const noexcept {
        return std::hash<teserra::u64>{}((teserra::u64{handle.generation()} << 32U) | handle.index());
    }
};

/// Formats as "<index>:<generation>", or "null".
template <typename Tag>
struct std::formatter<teserra::Handle<Tag>> // NOLINT(bugprone-std-namespace-modification): see std::hash
    : std::formatter<std::string_view> {
    auto format(teserra::Handle<Tag> handle, std::format_context& context) const {
        if (handle.isNull()) {
            return std::formatter<std::string_view>::format("null", context);
        }
        return std::format_to(context.out(), "{}:{}", handle.index(), handle.generation());
    }
};
