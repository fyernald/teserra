#pragma once

// Mount points (docs/architecture/assets.md#mount-points-and-files): the schemes of virtual paths map to
// directories, and all asset IO reads through them.
//
//   MountTable mounts{platform.fileSystem()};
//   (void)mounts.mount("assets", "C:/Games/Hunt/assets");
//   mounts.resolve("assets://sprites/hero.png");    // "C:/Games/Hunt/assets/sprites/hero.png"
//   mounts.readFile("assets://sprites/hero.png");   // its bytes
//   mounts.resolve("assets://../save.dat");          // InvalidArgument: ".." cannot leave the mount
//
// A virtual path is "<scheme>://<relative path>". The relative path uses '/' ('\' counts as '/' too) and
// every component is a name: empty components, "." and ".." are rejected rather than collapsed, so each
// file has one spelling and one AssetId. ':' is rejected after the scheme, so no drive letter or Windows
// stream name reaches the file system.

#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/platform/file_system.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace teserra::assets {

/// Maps the schemes of virtual paths ("assets", "engine") to directories and reads files through them.
/// Not thread-safe: asset loading is single-threaded in v1.
class MountTable {
public:
    /// Reads through `fileSystem`, which must outlive the table.
    explicit MountTable(platform::FileSystem& fileSystem) noexcept
        : m_fileSystem(&fileSystem) {}

    /// Makes virtual paths "<scheme>://..." refer to files under `directory`. The scheme is a letter followed
    /// by letters, digits, '-' or '_'; it is case-insensitive and stored lowercase. `directory` is a path of
    /// the file system, used as given: '\' becomes '/' and a trailing separator is dropped. Whether it
    /// exists is not checked.
    ///
    /// Errors: InvalidArgument (the scheme is not valid, or `directory` is empty), AlreadyExists (the scheme
    /// is mounted already; unmount() it first).
    [[nodiscard]] Result<void> mount(std::string_view scheme, std::string_view directory);

    /// Removes the mount of `scheme`; false if it was not mounted.
    bool unmount(std::string_view scheme) noexcept;

    /// The directory that `scheme` is mounted to, or std::nullopt.
    [[nodiscard]] std::optional<std::string_view> directory(std::string_view scheme) const noexcept;

    /// The file system path of `virtualPath`: its mount's directory, '/', and the relative path.
    ///
    /// Errors: InvalidArgument (`virtualPath` has no "<scheme>://", or its relative path is empty, has an
    /// empty, "." or ".." component, or contains ':'), NotFound (nothing is mounted for the scheme).
    [[nodiscard]] Result<std::string> resolve(std::string_view virtualPath) const;

    /// The whole content of the file at `virtualPath`: resolve(), then FileSystem::readFile(). Its errors
    /// name the virtual path.
    [[nodiscard]] Result<std::vector<std::byte>> readFile(std::string_view virtualPath) const;

    /// The file system the table reads through.
    [[nodiscard]] platform::FileSystem& fileSystem() const noexcept { return *m_fileSystem; }

    /// Number of mounted schemes.
    [[nodiscard]] usize size() const noexcept { return m_mounts.size(); }

private:
    struct Mount {
        std::string scheme; // lowercase
        std::string directory;
    };

    [[nodiscard]] const Mount* find(std::string_view scheme) const noexcept;

    platform::FileSystem* m_fileSystem;
    // A handful of mounts: a linear search beats a map.
    std::vector<Mount> m_mounts;
};

} // namespace teserra::assets
