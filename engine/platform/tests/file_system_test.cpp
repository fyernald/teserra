// NativeFileSystem: reading and writing whole files with UTF-8 paths, errors, and the executable and
// user-data directories. Files are written in a fresh directory under the system's temporary directory.

#include <teserra/platform/file_system.hpp>

#include <teserra/platform/platform.hpp>

#include "platform_test_support.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using namespace teserra;
using namespace teserra::platform;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;

namespace {

// UTF-8 text from a u8"" literal, as the FileSystem API takes it.
std::string utf8(std::u8string_view text) {
    return {text.begin(), text.end()};
}

std::string pathUtf8(const std::filesystem::path& path) {
    return utf8(path.generic_u8string());
}

std::filesystem::path pathFromUtf8(std::string_view text) {
    return {std::u8string(text.begin(), text.end())};
}

// A unique name, so that test runs in parallel or after a crash do not meet.
std::string uniqueName(std::string_view prefix) {
    return std::format("{}-{}", prefix, std::chrono::steady_clock::now().time_since_epoch().count());
}

/// A new empty directory, removed with everything in it at the end of the test.
class TemporaryDirectory {
public:
    TemporaryDirectory()
        : m_path(std::filesystem::temp_directory_path() / uniqueName("teserra_file_system_test")) {
        std::filesystem::create_directories(m_path);
    }
    // Removing may allocate; running out of memory in a test ends the run anyway.
    ~TemporaryDirectory() { // NOLINT(bugprone-exception-escape)
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&&) = delete;
    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }
    /// `name` inside the directory, as a UTF-8 path for the FileSystem API.
    [[nodiscard]] std::string file(std::u8string_view name) const { return pathUtf8(m_path / name); }

private:
    std::filesystem::path m_path;
};

std::vector<std::byte> everyByteValue() {
    std::vector<std::byte> bytes(256);
    for (usize value = 0; value < bytes.size(); ++value) {
        bytes[value] = static_cast<std::byte>(value);
    }
    return bytes;
}

std::vector<std::byte> bytesOf(std::string_view text) {
    const auto bytes = std::as_bytes(std::span(text));
    return {bytes.begin(), bytes.end()};
}

template <typename T>
void requireError(const Result<T>& result, ErrorCode code, std::string_view messagePart) {
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == code);
    CHECK(result.error().module == Module::Platform);
    CHECK_THAT(result.error().message, ContainsSubstring(std::string(messagePart)));
}

} // namespace

TEST_CASE("NativeFileSystem: writeFile and readFile round-trip the bytes of a file",
          "[platform][file_system]") {
    const TemporaryDirectory directory;
    NativeFileSystem fileSystem;
    const std::string path = directory.file(u8"data.bin");

    CHECK_FALSE(fileSystem.exists(path));
    const std::vector<std::byte> bytes = everyByteValue();
    REQUIRE(fileSystem.writeFile(path, bytes));
    CHECK(fileSystem.exists(path));

    const auto read = fileSystem.readFile(path);
    REQUIRE(read.has_value());
    CHECK(*read == bytes);

    SECTION("overwriting replaces the whole content and leaves no temporary file") {
        REQUIRE(fileSystem.writeFile(path, bytesOf("short")));

        const auto reread = fileSystem.readFile(path);
        REQUIRE(reread.has_value());
        CHECK(*reread == bytesOf("short"));
        CHECK_FALSE(fileSystem.exists(path + ".tmp"));
    }
    SECTION("an empty file") {
        REQUIRE(fileSystem.writeFile(path, {}));

        const auto reread = fileSystem.readFile(path);
        REQUIRE(reread.has_value());
        CHECK(reread->empty());
    }
    SECTION("exists is also true for a directory") {
        CHECK(fileSystem.exists(pathUtf8(directory.path())));
    }
}

TEST_CASE("NativeFileSystem: non-ASCII paths round-trip", "[platform][file_system]") {
    const TemporaryDirectory directory;
    NativeFileSystem fileSystem;
    // Turkish, Japanese and an emoji outside the Basic Multilingual Plane (a surrogate pair on Windows).
    const std::u8string_view folderName = u8"Kayıt 日本語";
    const std::u8string_view fileName = u8"çğüşöı-😀.sav";
    std::filesystem::create_directory(directory.path() / folderName);
    const std::string path = pathUtf8(directory.path() / folderName / fileName);

    REQUIRE(fileSystem.writeFile(path, bytesOf("progress")));
    CHECK(fileSystem.exists(path));
    const auto read = fileSystem.readFile(path);
    REQUIRE(read.has_value());
    CHECK(*read == bytesOf("progress"));

    // The OS sees the same name, not a mangled one: the path was never converted through a code page.
    std::vector<std::u8string> names;
    for (const auto& entry : std::filesystem::directory_iterator(directory.path() / folderName)) {
        names.push_back(entry.path().filename().u8string());
    }
    REQUIRE(names.size() == 1);
    CHECK(names[0] == fileName);
}

TEST_CASE("NativeFileSystem: errors name the path", "[platform][file_system]") {
    const TemporaryDirectory directory;
    NativeFileSystem fileSystem;

    SECTION("reading a missing file is NotFound") {
        const std::string path = directory.file(u8"missing.txt");
        requireError(fileSystem.readFile(path), ErrorCode::NotFound, path);
    }
    SECTION("writing into a missing directory is NotFound") {
        const std::string path = directory.file(u8"no/such/directory/file.txt");
        requireError(fileSystem.writeFile(path, bytesOf("x")), ErrorCode::NotFound, path);
        CHECK_FALSE(fileSystem.exists(path + ".tmp"));
    }
    SECTION("reading or writing a directory is InvalidArgument") {
        const std::string path = pathUtf8(directory.path());
        requireError(fileSystem.readFile(path), ErrorCode::InvalidArgument, path);
        requireError(fileSystem.writeFile(path, bytesOf("x")), ErrorCode::InvalidArgument, path);
    }
    SECTION("an empty path is InvalidArgument") {
        requireError(fileSystem.readFile(""), ErrorCode::InvalidArgument, "empty");
        requireError(fileSystem.writeFile("", bytesOf("x")), ErrorCode::InvalidArgument, "empty");
        CHECK_FALSE(fileSystem.exists(""));
    }
    SECTION("a path that is not valid UTF-8 is InvalidArgument") {
        const std::string base = directory.file(u8"bad-");
        // A lead byte without its continuation, an overlong '/', and an encoded surrogate.
        for (const std::string_view invalid : {"\xC3(", "\xC0\xAF", "\xED\xA0\x80"}) {
            const std::string path = base + std::string(invalid);
            INFO("bytes after 'bad-': " << invalid.size());
            requireError(fileSystem.writeFile(path, bytesOf("x")), ErrorCode::InvalidArgument,
                         "not valid UTF-8");
            requireError(fileSystem.readFile(path), ErrorCode::InvalidArgument, "not valid UTF-8");
            CHECK_FALSE(fileSystem.exists(path));
        }
    }
    SECTION("a path with a null character is InvalidArgument") {
        const std::string path = directory.file(u8"a") + std::string("\0b", 2);
        requireError(fileSystem.writeFile(path, bytesOf("x")), ErrorCode::InvalidArgument, "null character");
        CHECK_FALSE(fileSystem.exists(directory.file(u8"a")));
    }
}

TEST_CASE("NativeFileSystem: executableDirectory holds the running executable", "[platform][file_system]") {
    NativeFileSystem fileSystem;

    const auto directory = fileSystem.executableDirectory();

    REQUIRE(directory.has_value());
    CHECK(pathFromUtf8(*directory).is_absolute());
    CHECK(directory->find('\\') == std::string::npos);
    CHECK_FALSE(directory->ends_with('/'));
    CHECK(fileSystem.exists(*directory + "/" + TESERRA_TEST_EXECUTABLE_NAME));
}

TEST_CASE("NativeFileSystem: userDataDirectory creates a directory for the application",
          "[platform][file_system]") {
    NativeFileSystem fileSystem;

    SECTION("a valid name") {
        const std::string appName = uniqueName(utf8(u8"Teserra Test Ü"));

        const auto directory = fileSystem.userDataDirectory(appName);

        REQUIRE(directory.has_value());
        CHECK_THAT(*directory, EndsWith("/" + appName));
        CHECK(pathFromUtf8(*directory).is_absolute());
        CHECK(directory->find('\\') == std::string::npos);
        CHECK(fileSystem.exists(*directory));
        CHECK(fileSystem.writeFile(*directory + "/settings.json", bytesOf("{}")));
        // An existing directory is fine too.
        CHECK(fileSystem.userDataDirectory(appName) == directory);

        std::error_code ignored;
        std::filesystem::remove_all(pathFromUtf8(*directory), ignored);
    }
    SECTION("names that are not a single portable directory name are InvalidArgument") {
        for (const std::string_view name :
             {"", ".", "..", "a/b", "a\\b", "C:", "game?", "tab\tname", "dot.", "space "}) {
            INFO("app name '" << name << "'");
            requireError(fileSystem.userDataDirectory(name), ErrorCode::InvalidArgument,
                         "application directory");
        }
    }
}

TEST_CASE("Platform: fileSystem() is the native file system", "[platform][file_system]") {
    const auto platform = test::createPlatformOrSkip();
    FileSystem& fileSystem = platform->fileSystem();

    const auto directory = fileSystem.executableDirectory();

    REQUIRE(directory.has_value());
    CHECK(fileSystem.exists(*directory + "/" + TESERRA_TEST_EXECUTABLE_NAME));
}
