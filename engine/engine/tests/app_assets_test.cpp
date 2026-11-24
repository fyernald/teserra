// The App's asset services: mount points from AppConfig::mounts and their defaults.

#include <teserra/engine/app.hpp>
#include <teserra/test/memory_file_system.hpp>

#include "headless_platform.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace teserra;
using namespace teserra::engine;
using Catch::Matchers::ContainsSubstring;

namespace {

// An App without a device on a headless platform whose files are in memory, with the executable in /game.
struct AssetsApp {
    explicit AssetsApp(std::vector<AssetMount> mounts,
                       const std::function<void(test::MemoryFileSystem&)>& addFiles = {})
        : AssetsApp(std::move(mounts), std::make_unique<test::HeadlessPlatform>(), addFiles) {}

    AssetsApp(std::vector<AssetMount> mounts, std::unique_ptr<test::HeadlessPlatform> platform,
              const std::function<void(test::MemoryFileSystem&)>& addFiles)
        : files(platform->useFileSystem(std::make_unique<test::MemoryFileSystem>("/game"))),
          app(config(std::move(mounts), files, addFiles), std::move(platform)) {}

    test::MemoryFileSystem& files;
    App app;

private:
    static AppConfig config(std::vector<AssetMount> mounts, test::MemoryFileSystem& files,
                            const std::function<void(test::MemoryFileSystem&)>& addFiles) {
        if (addFiles) {
            addFiles(files);
        }
        return {.title = "Assets test", .backend = std::nullopt, .mounts = std::move(mounts)};
    }
};

} // namespace

TEST_CASE("App: mounts assets:// and engine:// next to the executable by default", "[engine][app][assets]") {
    AssetsApp fixture{{}};

    REQUIRE(fixture.app.isInitialized());
    assets::MountTable& mounts = fixture.app.mounts();
    CHECK(mounts.size() == 2);
    CHECK(mounts.directory("assets") == "/game/assets");
    CHECK(mounts.directory("engine") == "/game/engine");
    CHECK(&mounts.fileSystem() == &fixture.files);
    // The default directories may be missing: a game without files still starts.
    CHECK_FALSE(fixture.files.exists("/game/assets"));
}

TEST_CASE("App: reads files through its mounts", "[engine][app][assets]") {
    AssetsApp fixture{
        {}, [](test::MemoryFileSystem& files) { files.addTextFile("/game/assets/hero.png", "PNG"); }};

    REQUIRE(fixture.app.isInitialized());
    const Result<std::vector<std::byte>> bytes = fixture.app.mounts().readFile("assets://hero.png");
    REQUIRE(bytes.has_value());
    CHECK(bytes->size() == 3);
}

TEST_CASE("App: AppConfig::mounts adds schemes and replaces defaults", "[engine][app][assets]") {
    AssetsApp fixture{{{.scheme = "assets", .directory = "data"},
                       {.scheme = "mods", .directory = "/home/player/mods"},
                       {.scheme = "dlc", .directory = R"(C:\Games\Dlc)"}},
                      [](test::MemoryFileSystem& files) {
                          files.addDirectory("/game/data");
                          files.addDirectory("/home/player/mods");
                          files.addDirectory(R"(C:\Games\Dlc)");
                      }};

    REQUIRE(fixture.app.isInitialized());
    assets::MountTable& mounts = fixture.app.mounts();
    CHECK(mounts.size() == 4);
    // Relative directories are relative to the executable's, not the working directory.
    CHECK(mounts.directory("assets") == "/game/data");
    CHECK(mounts.directory("mods") == "/home/player/mods");
    CHECK(mounts.directory("dlc") == "C:/Games/Dlc");
    CHECK(mounts.directory("engine") == "/game/engine");
}

TEST_CASE("App: a listed directory that does not exist fails initialization", "[engine][app][assets]") {
    AssetsApp fixture{{{.scheme = "assets", .directory = "data"}}};

    REQUIRE_FALSE(fixture.app.isInitialized());
    CHECK(fixture.app.initError()->code == ErrorCode::NotFound);
    CHECK_THAT(fixture.app.initError()->message, ContainsSubstring("/game/data"));
    CHECK_THAT(fixture.app.initError()->message, ContainsSubstring("assets://"));
    CHECK(fixture.app.run() == 1);
}

TEST_CASE("App: an invalid or repeated mount fails initialization", "[engine][app][assets]") {
    const auto addData = [](test::MemoryFileSystem& files) { files.addDirectory("/game/data"); };

    SECTION("an invalid scheme") {
        AssetsApp fixture{{{.scheme = "my assets", .directory = "data"}}, addData};
        REQUIRE_FALSE(fixture.app.isInitialized());
        CHECK(fixture.app.initError()->code == ErrorCode::InvalidArgument);
    }
    SECTION("an empty directory") {
        AssetsApp fixture{{{.scheme = "assets", .directory = ""}}, addData};
        REQUIRE_FALSE(fixture.app.isInitialized());
        CHECK(fixture.app.initError()->code == ErrorCode::InvalidArgument);
    }
    SECTION("a scheme listed twice") {
        AssetsApp fixture{
            {{.scheme = "assets", .directory = "data"}, {.scheme = "Assets", .directory = "data"}}, addData};
        REQUIRE_FALSE(fixture.app.isInitialized());
        CHECK(fixture.app.initError()->code == ErrorCode::AlreadyExists);
    }
}
