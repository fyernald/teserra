// NativeFileSystem::userDataDirectory() on Linux: the XDG Base Directory rules for $XDG_DATA_HOME and $HOME.
// The tests point both variables at a temporary directory, so nothing is written into the real home.

#include <teserra/platform/file_system.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

using namespace teserra;
using namespace teserra::platform;

namespace {

// Sets an environment variable for the length of a test and restores its old value.
class ScopedEnvironment {
public:
    ScopedEnvironment(const char* name, const std::optional<std::string>& value)
        : m_name(name) {
        if (const char* old = std::getenv(name)) {
            m_old = old;
        }
        apply(value);
    }
    ~ScopedEnvironment() { apply(m_old); }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;
    ScopedEnvironment(ScopedEnvironment&&) = delete;
    ScopedEnvironment& operator=(ScopedEnvironment&&) = delete;

private:
    void apply(const std::optional<std::string>& value) const noexcept {
        if (value) {
            ::setenv(m_name, value->c_str(), 1);
        } else {
            ::unsetenv(m_name);
        }
    }

    const char* m_name;
    std::optional<std::string> m_old;
};

// A new empty directory, removed with everything in it at the end of the test.
class TemporaryDirectory {
public:
    TemporaryDirectory()
        : m_path(std::filesystem::temp_directory_path() /
                 std::format("teserra_xdg_test-{}",
                             std::chrono::steady_clock::now().time_since_epoch().count())) {
        std::filesystem::create_directories(m_path);
    }
    ~TemporaryDirectory() { // NOLINT(bugprone-exception-escape): removing may allocate
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&&) = delete;
    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;

    [[nodiscard]] std::string string() const { return m_path.generic_string(); }

private:
    std::filesystem::path m_path;
};

} // namespace

TEST_CASE("NativeFileSystem (POSIX): userDataDirectory follows the XDG Base Directory rules",
          "[platform][file_system]") {
    const TemporaryDirectory temporary;
    const std::string home = temporary.string() + "/home";
    const std::string dataHome = temporary.string() + "/data";
    const ScopedEnvironment homeVariable("HOME", home);
    NativeFileSystem fileSystem;

    SECTION("$XDG_DATA_HOME when it is an absolute path") {
        const ScopedEnvironment xdg("XDG_DATA_HOME", dataHome);
        const Result<std::string> directory = fileSystem.userDataDirectory("Game");
        REQUIRE(directory.has_value());
        CHECK(*directory == dataHome + "/Game");
        CHECK(std::filesystem::is_directory(*directory));
    }
    SECTION("$HOME/.local/share when $XDG_DATA_HOME is unset") {
        const ScopedEnvironment xdg("XDG_DATA_HOME", std::nullopt);
        const Result<std::string> directory = fileSystem.userDataDirectory("Game");
        REQUIRE(directory.has_value());
        CHECK(*directory == home + "/.local/share/Game");
        CHECK(std::filesystem::is_directory(*directory));
    }
    SECTION("$HOME/.local/share when $XDG_DATA_HOME is empty or relative, which the spec says to ignore") {
        for (const std::string_view value : {"", "relative/data"}) {
            INFO("XDG_DATA_HOME='" << value << "'");
            const ScopedEnvironment xdg("XDG_DATA_HOME", std::string(value));
            const Result<std::string> directory = fileSystem.userDataDirectory("Game");
            REQUIRE(directory.has_value());
            CHECK(*directory == home + "/.local/share/Game");
        }
    }
    SECTION("an error when neither variable is an absolute path") {
        const ScopedEnvironment xdg("XDG_DATA_HOME", std::nullopt);
        const ScopedEnvironment noHome("HOME", std::nullopt);
        const Result<std::string> directory = fileSystem.userDataDirectory("Game");
        REQUIRE_FALSE(directory.has_value());
        CHECK(directory.error().code == ErrorCode::IoFailure);
    }
}
