#pragma once

// Stable asset identity (docs/architecture/assets.md#handles-and-ids): the 64-bit FNV-1a hash of the
// normalized virtual path. Scenes and maps store AssetIds; the AssetRegistry maps them back to paths.
//
//   const AssetId hero = AssetId::fromPath("assets://sprites/hero.png");
//   hero == AssetId::fromPath("ASSETS://sprites\\hero.png");   // same normalized path

#include <teserra/core/types.hpp>

#include <cstddef>
#include <format>
#include <functional>
#include <string>
#include <string_view>

namespace teserra::assets {

namespace detail {

// One character of the normalized path: the scheme (before "://") is lowercase and backslashes become
// forward slashes. The rest keeps its case, because file names may be case-sensitive (Linux).
[[nodiscard]] constexpr char normalizedChar(char character, bool inScheme) noexcept {
    if (character == '\\') {
        return '/';
    }
    if (inScheme && character >= 'A' && character <= 'Z') {
        return static_cast<char>(character - 'A' + 'a');
    }
    return character;
}

// Length of the scheme of `path`, or 0 when it has none.
[[nodiscard]] constexpr usize schemeLength(std::string_view path) noexcept {
    const usize separator = path.find("://");
    return separator == std::string_view::npos ? 0 : separator;
}

} // namespace detail

/// The normalized form of a virtual path: lowercase scheme, forward slashes. Rejecting ".." and resolving
/// mount points is the mount table's work (docs/architecture/assets.md#mount-points-and-files).
[[nodiscard]] inline std::string normalizeAssetPath(std::string_view virtualPath) {
    std::string normalized(virtualPath);
    const usize scheme = detail::schemeLength(virtualPath);
    for (usize index = 0; index < normalized.size(); ++index) {
        normalized[index] = detail::normalizedChar(normalized[index], index < scheme);
    }
    return normalized;
}

/// Identifies an asset by its virtual path. The value 0 means "no asset".
struct AssetId {
    u64 value = 0;

    /// The ID of `virtualPath`: FNV-1a 64-bit of normalizeAssetPath(virtualPath), computed without
    /// allocating.
    [[nodiscard]] static constexpr AssetId fromPath(std::string_view virtualPath) noexcept {
        const usize scheme = detail::schemeLength(virtualPath);
        u64 hash = 0xcbf2'9ce4'8422'2325ULL;
        for (usize index = 0; index < virtualPath.size(); ++index) {
            hash ^= static_cast<u8>(detail::normalizedChar(virtualPath[index], index < scheme));
            hash *= 0x0000'0100'0000'01b3ULL;
        }
        return AssetId{hash};
    }

    /// Whether this is the null id, which no asset has.
    [[nodiscard]] constexpr bool isNull() const noexcept { return value == 0; }

    friend constexpr bool operator==(AssetId, AssetId) noexcept = default;
    friend constexpr auto operator<=>(AssetId, AssetId) noexcept = default;
};

} // namespace teserra::assets

// Specializing std templates for program-defined types is allowed ([namespace.std]).
template <>
struct std::hash<teserra::assets::AssetId> { // NOLINT(bugprone-std-namespace-modification)
    [[nodiscard]] std::size_t operator()(teserra::assets::AssetId id) const noexcept {
        return static_cast<std::size_t>(id.value);
    }
};

/// Formats as 16 hexadecimal digits: "0x00c0ffee00c0ffee".
template <>
struct std::formatter<teserra::assets::AssetId> // NOLINT(bugprone-std-namespace-modification)
    : std::formatter<std::string_view> {
    auto format(teserra::assets::AssetId id, std::format_context& context) const {
        return std::format_to(context.out(), "{:#018x}", id.value);
    }
};
