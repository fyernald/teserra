#include <teserra/assets/mount_table.hpp>

#include <algorithm>
#include <utility>

namespace teserra::assets {

namespace {

constexpr std::string_view kSchemeSeparator = "://";

[[nodiscard]] constexpr char toLower(char character) noexcept {
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
}

[[nodiscard]] constexpr bool isLetter(char character) noexcept {
    const char lower = toLower(character);
    return lower >= 'a' && lower <= 'z';
}

// A letter followed by letters, digits, '-' or '_', in either case.
[[nodiscard]] constexpr bool isValidScheme(std::string_view scheme) noexcept {
    if (scheme.empty() || !isLetter(scheme.front())) {
        return false;
    }
    return std::ranges::all_of(scheme, [](char character) {
        return isLetter(character) || (character >= '0' && character <= '9') || character == '-' ||
               character == '_';
    });
}

[[nodiscard]] constexpr bool isSeparator(char character) noexcept {
    return character == '/' || character == '\\';
}

// InvalidArgument unless every component of `relative` (split at '/' and '\') is a name: not empty, not
// "." or "..", without ':'.
[[nodiscard]] Result<void> checkRelativePath(std::string_view virtualPath, std::string_view relative) {
    if (relative.empty()) {
        return fail(ErrorCode::InvalidArgument, Module::Assets, "Virtual path '{}' names no file",
                    virtualPath);
    }
    if (relative.find(':') != std::string_view::npos) {
        return fail(ErrorCode::InvalidArgument, Module::Assets,
                    "Virtual path '{}' contains ':' after its scheme; drive letters and absolute paths are "
                    "not allowed",
                    virtualPath);
    }
    usize start = 0;
    while (start <= relative.size()) {
        const auto end =
            std::find_if(relative.begin() + static_cast<std::ptrdiff_t>(start), relative.end(), isSeparator);
        const auto stop = static_cast<usize>(end - relative.begin());
        const std::string_view component = relative.substr(start, stop - start);
        if (component.empty()) {
            return fail(ErrorCode::InvalidArgument, Module::Assets,
                        "Virtual path '{}' has an empty component (a leading, trailing or doubled '/')",
                        virtualPath);
        }
        if (component == "." || component == "..") {
            return fail(ErrorCode::InvalidArgument, Module::Assets,
                        "Virtual path '{}' has a '{}' component; virtual paths cannot leave their mount and "
                        "have one spelling per file",
                        virtualPath, component);
        }
        start = stop + 1;
    }
    return {};
}

[[nodiscard]] bool equalsIgnoringCase(std::string_view lowercase, std::string_view text) noexcept {
    return std::ranges::equal(lowercase, text, [](char left, char right) { return left == toLower(right); });
}

} // namespace

Result<void> MountTable::mount(std::string_view scheme, std::string_view directory) {
    if (!isValidScheme(scheme)) {
        return fail(ErrorCode::InvalidArgument, Module::Assets,
                    "Cannot mount scheme '{}': a scheme is a letter followed by letters, digits, '-' or '_'",
                    scheme);
    }
    if (directory.empty()) {
        return fail(ErrorCode::InvalidArgument, Module::Assets, "Cannot mount '{}://' to an empty directory",
                    scheme);
    }
    if (const Mount* mounted = find(scheme); mounted != nullptr) {
        return fail(ErrorCode::AlreadyExists, Module::Assets,
                    "Cannot mount '{}://' to '{}': it is mounted to '{}'", mounted->scheme, directory,
                    mounted->directory);
    }

    Mount added;
    added.scheme.reserve(scheme.size());
    std::ranges::transform(scheme, std::back_inserter(added.scheme), toLower);
    added.directory.reserve(directory.size());
    std::ranges::transform(directory, std::back_inserter(added.directory),
                           [](char character) { return character == '\\' ? '/' : character; });
    // "<directory>/<relative>" needs no separator at the end; the root "/" becomes "", which gives
    // "/<relative>".
    while (!added.directory.empty() && added.directory.back() == '/') {
        added.directory.pop_back();
    }
    m_mounts.push_back(std::move(added));
    return {};
}

bool MountTable::unmount(std::string_view scheme) noexcept {
    const auto found = std::ranges::find_if(
        m_mounts, [&](const Mount& mounted) { return equalsIgnoringCase(mounted.scheme, scheme); });
    if (found == m_mounts.end()) {
        return false;
    }
    m_mounts.erase(found);
    return true;
}

std::optional<std::string_view> MountTable::directory(std::string_view scheme) const noexcept {
    const Mount* mounted = find(scheme);
    return mounted != nullptr ? std::optional<std::string_view>(mounted->directory) : std::nullopt;
}

Result<std::string> MountTable::resolve(std::string_view virtualPath) const {
    const usize separator = virtualPath.find(kSchemeSeparator);
    if (separator == std::string_view::npos) {
        return fail(ErrorCode::InvalidArgument, Module::Assets,
                    "Virtual path '{}' has no scheme, such as 'assets://'", virtualPath);
    }
    const std::string_view scheme = virtualPath.substr(0, separator);
    if (!isValidScheme(scheme)) {
        return fail(ErrorCode::InvalidArgument, Module::Assets,
                    "Virtual path '{}' has an invalid scheme '{}': a scheme is a letter followed by letters, "
                    "digits, '-' or '_'",
                    virtualPath, scheme);
    }
    const std::string_view relative = virtualPath.substr(separator + kSchemeSeparator.size());
    TESERRA_TRY(checkRelativePath(virtualPath, relative));

    const Mount* mounted = find(scheme);
    if (mounted == nullptr) {
        return fail(ErrorCode::NotFound, Module::Assets, "Virtual path '{}': nothing is mounted for '{}://'",
                    virtualPath, scheme);
    }

    std::string resolved;
    resolved.reserve(mounted->directory.size() + 1 + relative.size());
    resolved.append(mounted->directory);
    resolved.push_back('/');
    std::ranges::transform(relative, std::back_inserter(resolved),
                           [](char character) { return character == '\\' ? '/' : character; });
    return resolved;
}

Result<std::vector<std::byte>> MountTable::readFile(std::string_view virtualPath) const {
    TESERRA_TRY_ASSIGN(const std::string path, resolve(virtualPath));
    Result<std::vector<std::byte>> bytes = m_fileSystem->readFile(path);
    if (!bytes) {
        Error error = std::move(bytes.error());
        error.message = std::format("Cannot read '{}': {}", virtualPath, error.message);
        return Unexpected(std::move(error));
    }
    return bytes;
}

const MountTable::Mount* MountTable::find(std::string_view scheme) const noexcept {
    const auto found = std::ranges::find_if(
        m_mounts, [&](const Mount& mounted) { return equalsIgnoringCase(mounted.scheme, scheme); });
    return found != m_mounts.end() ? &*found : nullptr;
}

} // namespace teserra::assets
