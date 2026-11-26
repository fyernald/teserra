// The POSIX part of NativeFileSystem (Linux): the directories that only the OS knows. POSIX paths are bytes,
// which the API reads as UTF-8 (ADR-0015).

#include <teserra/platform/file_system.hpp>

#include "common/file_system_paths.hpp"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace teserra::platform {

namespace {

// The value of the environment variable `name` if it is set and holds an absolute path, else nothing.
// NativeFileSystem is used from any thread, and only reads the environment.
[[nodiscard]] std::optional<std::filesystem::path> absolutePathFromEnvironment(const char* name) {
    const char* value = std::getenv(name); // the engine never sets it
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    Result<std::filesystem::path> path = toPath(value);
    if (!path || !path->is_absolute()) {
        return std::nullopt;
    }
    return std::move(*path);
}

} // namespace

Result<std::string> NativeFileSystem::executableDirectory() {
    std::error_code error;
    const std::filesystem::path executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) {
        return fail(ErrorCode::IoFailure, Module::Platform,
                    "Cannot get the executable's path from /proc/self/exe: {}", error.message());
    }
    return toUtf8(executable.parent_path());
}

Result<std::string> NativeFileSystem::userDataDirectory(std::string_view appName) {
    TESERRA_TRY(checkAppName(appName));
    TESERRA_TRY_ASSIGN(const std::filesystem::path name, toPath(appName));

    // The XDG Base Directory specification: $XDG_DATA_HOME, or $HOME/.local/share when it is unset, empty or
    // relative (a relative value is invalid and must be ignored).
    std::filesystem::path base;
    if (std::optional<std::filesystem::path> dataHome = absolutePathFromEnvironment("XDG_DATA_HOME")) {
        base = std::move(*dataHome);
    } else if (std::optional<std::filesystem::path> home = absolutePathFromEnvironment("HOME")) {
        base = *home / ".local" / "share";
    } else {
        return fail(ErrorCode::IoFailure, Module::Platform,
                    "Cannot find the user's data directory for '{}': neither XDG_DATA_HOME nor HOME is an "
                    "absolute path",
                    appName);
    }

    const std::filesystem::path directory = base / name;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return fileError(error, "create the directory", toUtf8(directory));
    }
    return toUtf8(directory);
}

} // namespace teserra::platform
