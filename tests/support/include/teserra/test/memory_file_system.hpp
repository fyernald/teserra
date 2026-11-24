#pragma once

// Test helper: a platform::FileSystem in memory, with a chosen executable directory. Header-only, because
// only tests that link teserra_platform can use it: #include <teserra/test/memory_file_system.hpp>.
//
//   test::MemoryFileSystem files{"/game"};
//   files.addFile("/game/assets/hero.png", bytes);   // its parent directories exist from now on
//   files.addDirectory("/game/mods");

#include <teserra/core/result.hpp>
#include <teserra/platform/file_system.hpp>

#include <cstddef>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace teserra::test {

class MemoryFileSystem final : public platform::FileSystem {
public:
    explicit MemoryFileSystem(std::string executableDirectory = "/game")
        : m_executableDirectory(std::move(executableDirectory)) {
        addDirectory(m_executableDirectory);
    }

    /// Adds or replaces the file at `path` (exactly as given), and makes its parent directories exist.
    void addFile(std::string path, std::vector<std::byte> bytes) {
        addParents(path);
        m_files.insert_or_assign(std::move(path), std::move(bytes));
    }

    /// Adds the file at `path` with `text` as its bytes.
    void addTextFile(std::string path, std::string_view text) {
        std::vector<std::byte> bytes(text.size());
        for (std::size_t index = 0; index < text.size(); ++index) {
            bytes[index] = static_cast<std::byte>(text[index]);
        }
        addFile(std::move(path), std::move(bytes));
    }

    /// Makes `path` and its parents exist as directories.
    void addDirectory(const std::string& path) {
        addParents(path);
        m_directories.insert(path);
    }

    bool removeFile(const std::string& path) { return m_files.erase(path) != 0; }

    /// Paths readFile() was called with, in order.
    [[nodiscard]] const std::vector<std::string>& reads() const noexcept { return m_reads; }

    [[nodiscard]] Result<std::vector<std::byte>> readFile(std::string_view path) override {
        m_reads.emplace_back(path);
        const auto found = m_files.find(path);
        if (found == m_files.end()) {
            return fail(ErrorCode::NotFound, Module::Platform, "File '{}' not found", path);
        }
        return found->second;
    }

    [[nodiscard]] Result<void> writeFile(std::string_view path, std::span<const std::byte> bytes) override {
        addFile(std::string(path), {bytes.begin(), bytes.end()});
        return {};
    }

    [[nodiscard]] bool exists(std::string_view path) override {
        return m_files.contains(path) || m_directories.contains(path);
    }

    [[nodiscard]] Result<std::string> executableDirectory() override { return m_executableDirectory; }

    [[nodiscard]] Result<std::string> userDataDirectory(std::string_view appName) override {
        std::string directory = "/user/" + std::string(appName);
        addDirectory(directory);
        return directory;
    }

private:
    void addParents(const std::string& path) {
        for (std::size_t slash = path.find('/', 1); slash != std::string::npos;
             slash = path.find('/', slash + 1)) {
            m_directories.insert(path.substr(0, slash));
        }
    }

    std::string m_executableDirectory;
    std::map<std::string, std::vector<std::byte>, std::less<>> m_files;
    std::set<std::string, std::less<>> m_directories;
    std::vector<std::string> m_reads;
};

} // namespace teserra::test
