#pragma once

// File access (docs/architecture/platform-abstraction.md#interfaces, ADR-0015). Paths are UTF-8 strings
// at the API on every OS; std::filesystem::path is only used inside the platform layer.

#include <teserra/core/result.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace teserra::platform {

/// Reads and writes whole files and finds the directories a game uses.
///
/// Paths are UTF-8. Components are separated by `/` (on Windows `\` works too); a relative path is
/// relative to the current working directory, so build paths from executableDirectory() or
/// userDataDirectory() instead. Every error message names the path.
class FileSystem {
public:
    virtual ~FileSystem() = default;

    /// The whole content of the file at `path`.
    ///
    /// Errors: NotFound (the file does not exist), AccessDenied, InvalidArgument (`path` is empty, not
    /// valid UTF-8 or a directory), IoFailure (reading failed).
    [[nodiscard]] virtual Result<std::vector<std::byte>> readFile(std::string_view path) = 0;

    /// Replaces the content of the file at `path` with `bytes`, creating the file if needed. The parent
    /// directory must exist. The bytes are written to `<path>.tmp` first, which then replaces the file, so
    /// a crash during the write leaves the previous content.
    ///
    /// Errors: NotFound (the parent directory does not exist), AccessDenied, InvalidArgument (`path` is
    /// empty, not valid UTF-8 or a directory), IoFailure (writing failed, e.g. the disk is full).
    [[nodiscard]] virtual Result<void> writeFile(std::string_view path, std::span<const std::byte> bytes) = 0;

    /// Whether a file or directory exists at `path`. False for paths that cannot be checked, such as an
    /// empty path or invalid UTF-8.
    [[nodiscard]] virtual bool exists(std::string_view path) = 0;

    /// The absolute directory of the running executable, with `/` separators and no trailing separator.
    [[nodiscard]] virtual Result<std::string> executableDirectory() = 0;

    /// The absolute directory where the game `appName` keeps saves and settings for the current user,
    /// with `/` separators and no trailing separator; created if it does not exist. On Windows it is
    /// `%APPDATA%\<appName>`.
    ///
    /// Errors: InvalidArgument (`appName` is empty, `.` or `..`, or contains a separator, a control
    /// character or one of `:*?"<>|`, or ends with a dot or a space), AccessDenied, IoFailure.
    [[nodiscard]] virtual Result<std::string> userDataDirectory(std::string_view appName) = 0;

protected:
    FileSystem() = default;
    FileSystem(const FileSystem&) = default;
    FileSystem& operator=(const FileSystem&) = default;
    FileSystem(FileSystem&&) = default;
    FileSystem& operator=(FileSystem&&) = default;
};

/// The operating system's file system. Stateless and thread-safe; Platform::fileSystem() returns one, and
/// code without a Platform (tools, tests) can create its own.
class NativeFileSystem final : public FileSystem {
public:
    [[nodiscard]] Result<std::vector<std::byte>> readFile(std::string_view path) override;
    [[nodiscard]] Result<void> writeFile(std::string_view path, std::span<const std::byte> bytes) override;
    [[nodiscard]] bool exists(std::string_view path) override;
    [[nodiscard]] Result<std::string> executableDirectory() override;
    [[nodiscard]] Result<std::string> userDataDirectory(std::string_view appName) override;
};

} // namespace teserra::platform
