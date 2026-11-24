// MountTable: schemes, resolving virtual paths, rejecting paths that would leave their mount, and reading
// files through mounts.

#include <teserra/assets/mount_table.hpp>

#include <teserra/core/result.hpp>
#include <teserra/test/memory_file_system.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using namespace teserra;
using namespace teserra::assets;
using Catch::Matchers::ContainsSubstring;

namespace {

// The error code of `result`, which must have failed.
template <typename T>
ErrorCode errorCode(const Result<T>& result) {
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().module == Module::Assets);
    return result.error().code;
}

} // namespace

TEST_CASE("MountTable: resolves virtual paths below the mounted directory", "[assets][mount_table]") {
    test::MemoryFileSystem files;
    MountTable mounts{files};
    REQUIRE(mounts.mount("assets", "/game/assets"));
    REQUIRE(mounts.mount("engine", "/opt/teserra/engine"));

    CHECK(mounts.size() == 2);
    CHECK(mounts.resolve("assets://hero.png") == "/game/assets/hero.png");
    CHECK(mounts.resolve("assets://sprites/hero.png") == "/game/assets/sprites/hero.png");
    CHECK(mounts.resolve("engine://shaders/sprite.tshader") == "/opt/teserra/engine/shaders/sprite.tshader");
    // The scheme is case-insensitive and '\' separates like '/', as in normalizeAssetPath().
    CHECK(mounts.resolve("ASSETS://sprites\\hero.png") == "/game/assets/sprites/hero.png");
    // Names keep their case (Linux file systems are case-sensitive) and may contain dots.
    CHECK(mounts.resolve("assets://Maps/level.1.tmj") == "/game/assets/Maps/level.1.tmj");
    CHECK(mounts.resolve("assets://.hidden/a..b") == "/game/assets/.hidden/a..b");
}

TEST_CASE("MountTable: directories use '/' without a trailing separator", "[assets][mount_table]") {
    test::MemoryFileSystem files;
    MountTable mounts{files};
    REQUIRE(mounts.mount("assets", "C:\\Games\\Hunt\\assets\\"));
    REQUIRE(mounts.mount("data", "/srv/data//"));
    REQUIRE(mounts.mount("root", "/"));

    CHECK(mounts.directory("assets") == "C:/Games/Hunt/assets");
    CHECK(mounts.directory("Data") == "/srv/data");
    CHECK(mounts.resolve("assets://hero.png") == "C:/Games/Hunt/assets/hero.png");
    CHECK(mounts.resolve("root://etc/game.cfg") == "/etc/game.cfg");
    CHECK_FALSE(mounts.directory("engine").has_value());
}

TEST_CASE("MountTable: schemes are letters, digits, '-' and '_', starting with a letter",
          "[assets][mount_table]") {
    test::MemoryFileSystem files;
    MountTable mounts{files};
    CHECK(mounts.mount("assets", "/a"));
    CHECK(mounts.mount("Mod_2-extra", "/b"));
    CHECK(mounts.directory("mod_2-extra") == "/b");

    for (const std::string_view scheme : {"", "2d", "-mod", "my mod", "assets:", "a/b", "a.b", "\xC3\xA9"}) {
        CAPTURE(scheme);
        const Result<void> mounted = mounts.mount(scheme, "/c");
        CHECK(errorCode(mounted) == ErrorCode::InvalidArgument);
    }
    CHECK(mounts.size() == 2);
}

TEST_CASE("MountTable: mounting a scheme twice or to an empty directory fails", "[assets][mount_table]") {
    test::MemoryFileSystem files;
    MountTable mounts{files};
    REQUIRE(mounts.mount("assets", "/game/assets"));

    const Result<void> again = mounts.mount("Assets", "/elsewhere");
    CHECK(errorCode(again) == ErrorCode::AlreadyExists);
    CHECK_THAT(again.error().message, ContainsSubstring("/game/assets"));
    CHECK(mounts.directory("assets") == "/game/assets");

    CHECK(errorCode(mounts.mount("engine", "")) == ErrorCode::InvalidArgument);
}

TEST_CASE("MountTable: unmount() removes a scheme", "[assets][mount_table]") {
    test::MemoryFileSystem files;
    MountTable mounts{files};
    REQUIRE(mounts.mount("assets", "/game/assets"));

    CHECK(mounts.unmount("ASSETS"));
    CHECK_FALSE(mounts.unmount("assets"));
    CHECK(mounts.size() == 0);
    CHECK(errorCode(mounts.resolve("assets://hero.png")) == ErrorCode::NotFound);
    // The scheme can be mounted again, elsewhere.
    REQUIRE(mounts.mount("assets", "/mods/assets"));
    CHECK(mounts.resolve("assets://hero.png") == "/mods/assets/hero.png");
}

TEST_CASE("MountTable: paths that would leave their mount are rejected with an Error",
          "[assets][mount_table]") {
    test::MemoryFileSystem files;
    MountTable mounts{files};
    REQUIRE(mounts.mount("assets", "/game/assets"));

    SECTION("'..' components") {
        for (const std::string_view path :
             {"assets://..", "assets://../save.dat", "assets://sprites/../../save.dat", "assets://sprites/..",
              "assets://sprites\\..\\..\\save.dat"}) {
            CAPTURE(path);
            const Result<std::string> resolved = mounts.resolve(path);
            CHECK(errorCode(resolved) == ErrorCode::InvalidArgument);
            CHECK_THAT(resolved.error().message, ContainsSubstring(std::string(path)));
            CHECK_THAT(resolved.error().message, ContainsSubstring("'..'"));
        }
    }
    SECTION("absolute paths and drive letters") {
        for (const std::string_view path :
             {"assets:///etc/passwd", "assets://\\Windows\\win.ini", "assets://C:/Windows/win.ini",
              "assets://C:secret.txt", "assets://hero.png:stream"}) {
            CAPTURE(path);
            CHECK(errorCode(mounts.resolve(path)) == ErrorCode::InvalidArgument);
        }
    }
    SECTION("'.' and empty components, so each file has one spelling") {
        for (const std::string_view path : {"assets://./hero.png", "assets://sprites/./hero.png",
                                            "assets://sprites//hero.png", "assets://sprites/", "assets://"}) {
            CAPTURE(path);
            CHECK(errorCode(mounts.resolve(path)) == ErrorCode::InvalidArgument);
        }
    }
    SECTION("no scheme or an invalid one") {
        for (const std::string_view path : {"hero.png", "/game/assets/hero.png", "C:/game/assets/hero.png",
                                            "://hero.png", "as sets://hero.png", "assets:/hero.png"}) {
            CAPTURE(path);
            CHECK(errorCode(mounts.resolve(path)) == ErrorCode::InvalidArgument);
        }
    }
    CHECK(files.reads().empty());
}

TEST_CASE("MountTable: a scheme without a mount is NotFound", "[assets][mount_table]") {
    test::MemoryFileSystem files;
    MountTable mounts{files};
    REQUIRE(mounts.mount("assets", "/game/assets"));

    const Result<std::string> resolved = mounts.resolve("engine://shaders/sprite.tshader");
    CHECK(errorCode(resolved) == ErrorCode::NotFound);
    CHECK_THAT(resolved.error().message, ContainsSubstring("engine://"));
}

TEST_CASE("MountTable: readFile() reads through the mount", "[assets][mount_table]") {
    test::MemoryFileSystem files;
    files.addTextFile("/game/assets/sprites/hero.png", "PNG");
    MountTable mounts{files};
    REQUIRE(mounts.mount("assets", "/game/assets"));

    const Result<std::vector<std::byte>> bytes = mounts.readFile("assets://sprites\\hero.png");
    REQUIRE(bytes.has_value());
    CHECK(*bytes == std::vector<std::byte>{std::byte{'P'}, std::byte{'N'}, std::byte{'G'}});
    CHECK(files.reads() == std::vector<std::string>{"/game/assets/sprites/hero.png"});
    CHECK(&mounts.fileSystem() == &files);

    SECTION("a missing file keeps the file system's error and names the virtual path") {
        const Result<std::vector<std::byte>> missing = mounts.readFile("assets://sprites/coin.png");
        REQUIRE_FALSE(missing.has_value());
        CHECK(missing.error().code == ErrorCode::NotFound);
        CHECK(missing.error().module == Module::Platform);
        CHECK_THAT(missing.error().message, ContainsSubstring("assets://sprites/coin.png"));
        CHECK_THAT(missing.error().message, ContainsSubstring("/game/assets/sprites/coin.png"));
    }
    SECTION("an invalid path is not read") {
        CHECK(errorCode(mounts.readFile("assets://../hero.png")) == ErrorCode::InvalidArgument);
        CHECK(files.reads().size() == 1);
    }
}

TEST_CASE("Mount table: relative paths resolve against the file that names them", "[assets][mount]") {
    CHECK(resolveRelativePath("assets://maps/level.tmj", "../tiles/t.png") == "assets://tiles/t.png");
    CHECK(resolveRelativePath("assets://maps/level.tmj", "t.png") == "assets://maps/t.png");
    CHECK(resolveRelativePath("assets://maps/level.tmj", "./a/./b\\c.png") == "assets://maps/a/b/c.png");
    CHECK(resolveRelativePath("assets://level.tmj", "t.png") == "assets://t.png");
    CHECK(resolveRelativePath("assets://maps/level.tmj", "engine://fallback.png") == "engine://fallback.png");
    CHECK_FALSE(resolveRelativePath("assets://level.tmj", "../t.png").has_value());
}
