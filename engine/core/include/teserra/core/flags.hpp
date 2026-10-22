#pragma once

// Bit-flag operators for scoped enums. A flag enum has None = 0 and single bits, and TESERRA_FLAGS defines
// the operators for it in the enum's own namespace, where argument-dependent lookup finds them from any
// code:
//
//   enum class TextureUsage : u8 {
//       None = 0,
//       Sampled = 1U << 0U,
//       RenderTarget = 1U << 1U,
//       CopyDst = 1U << 5U,
//   };
//   TESERRA_FLAGS(TextureUsage)
//
//   TextureUsage usage = TextureUsage::Sampled | TextureUsage::CopyDst;
//   if (hasAny(usage, TextureUsage::RenderTarget)) { ... }
//
// Combinations stay of the enum type, so a TextureUsage cannot be passed where a BufferUsage is expected.
// The empty set is None, which is also the value-initialized enum.

#include <type_traits>

/// Defines `| & ^ ~ |= &= ^=`, `hasAll()` and `hasAny()` for the scoped enum `Enum`. Use it right after
/// the enum, in the same namespace.
// `Enum` names a type, which cannot be parenthesized where the operators declare it.
// NOLINTBEGIN(bugprone-macro-parentheses)
#define TESERRA_FLAGS(Enum)                                                                                  \
    [[nodiscard]] constexpr Enum operator|(Enum a, Enum b) noexcept {                                        \
        using Bits = std::underlying_type_t<Enum>;                                                           \
        return static_cast<Enum>(static_cast<Bits>(static_cast<Bits>(a) | static_cast<Bits>(b)));            \
    }                                                                                                        \
    [[nodiscard]] constexpr Enum operator&(Enum a, Enum b) noexcept {                                        \
        using Bits = std::underlying_type_t<Enum>;                                                           \
        return static_cast<Enum>(static_cast<Bits>(static_cast<Bits>(a) & static_cast<Bits>(b)));            \
    }                                                                                                        \
    [[nodiscard]] constexpr Enum operator^(Enum a, Enum b) noexcept {                                        \
        using Bits = std::underlying_type_t<Enum>;                                                           \
        return static_cast<Enum>(static_cast<Bits>(static_cast<Bits>(a) ^ static_cast<Bits>(b)));            \
    }                                                                                                        \
    [[nodiscard]] constexpr Enum operator~(Enum a) noexcept {                                                \
        using Bits = std::underlying_type_t<Enum>;                                                           \
        return static_cast<Enum>(static_cast<Bits>(~static_cast<Bits>(a)));                                  \
    }                                                                                                        \
    constexpr Enum& operator|=(Enum& a, Enum b) noexcept {                                                   \
        return a = a | b;                                                                                    \
    }                                                                                                        \
    constexpr Enum& operator&=(Enum& a, Enum b) noexcept {                                                   \
        return a = a & b;                                                                                    \
    }                                                                                                        \
    constexpr Enum& operator^=(Enum& a, Enum b) noexcept {                                                   \
        return a = a ^ b;                                                                                    \
    }                                                                                                        \
    /** True if every bit of `bits` is set in `value` (and for an empty `bits`). */                          \
    [[nodiscard]] constexpr bool hasAll(Enum value, Enum bits) noexcept {                                    \
        return (value & bits) == bits;                                                                       \
    }                                                                                                        \
    /** True if any bit of `bits` is set in `value`. */                                                      \
    [[nodiscard]] constexpr bool hasAny(Enum value, Enum bits) noexcept {                                    \
        return (value & bits) != Enum{};                                                                     \
    }
// NOLINTEND(bugprone-macro-parentheses)
