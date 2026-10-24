#pragma once

// Conversions between the UTF-8 paths of the FileSystem API and std::filesystem::path, shared by the
// portable and the OS-specific parts of NativeFileSystem.

#include <teserra/core/result.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace teserra::platform {

/// `path` as a std::filesystem::path. InvalidArgument if it is empty, not valid UTF-8 or contains a null
/// character. Checking first means std::filesystem never throws for an invalid encoding (ADR-0016).
[[nodiscard]] Result<std::filesystem::path> toPath(std::string_view path);

/// `path` as UTF-8 with `/` separators. path::string() would use the ANSI code page on Windows.
[[nodiscard]] std::string toUtf8(const std::filesystem::path& path);

/// InvalidArgument unless `appName` can be a directory name on every supported OS (see
/// FileSystem::userDataDirectory()).
[[nodiscard]] Result<void> checkAppName(std::string_view appName);

/// The error for `error` while doing `action` ("read", "write", ...) on `path`: NotFound, AccessDenied or
/// IoFailure, with the path and the system's message.
[[nodiscard]] Unexpected fileError(std::error_code error, std::string_view action, std::string_view path);

} // namespace teserra::platform
