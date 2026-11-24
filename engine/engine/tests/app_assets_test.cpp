// The App's asset services: mount points from AppConfig::mounts and their defaults, and the asset registry
// with its loaders, unloading at the end of each frame.

#include <teserra/assets/image.hpp>
#include <teserra/engine/app.hpp>
#include <teserra/renderer/assets.hpp>
#include <teserra/rhi_null/null_device.hpp>
#include <teserra/test/memory_file_system.hpp>
#include <teserra/test/png.hpp>

#include "headless_platform.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace teserra;
using namespace teserra::engine;
using Catch::Matchers::ContainsSubstring;

namespace {

using AddFiles = std::function<void(test::MemoryFileSystem&)>;

// An App on a headless platform whose files are in memory, with the executable in /game; without a device
// unless `backend` is given.
struct AssetsApp {
    explicit AssetsApp(std::vector<AssetMount> mounts, const AddFiles& addFiles = {},
                       std::optional<rhi::Backend> backend = std::nullopt)
        : AssetsApp(std::move(mounts), std::make_unique<test::HeadlessPlatform>(), addFiles, backend) {}

    AssetsApp(std::vector<AssetMount> mounts, std::unique_ptr<test::HeadlessPlatform> platform,
              const AddFiles& addFiles, std::optional<rhi::Backend> backend)
        : files(platform->useFileSystem(std::make_unique<test::MemoryFileSystem>("/game"))),
          app(config(std::move(mounts), files, addFiles, backend), std::move(platform)) {}

    test::MemoryFileSystem& files;
    App app;

private:
    static AppConfig config(std::vector<AssetMount> mounts, test::MemoryFileSystem& files,
                            const AddFiles& addFiles, std::optional<rhi::Backend> backend) {
        if (addFiles) {
            addFiles(files);
        }
        return {.title = "Assets test", .backend = backend, .mounts = std::move(mounts)};
    }
};

// A 1 x 1 opaque red PNG.
std::vector<std::byte> redPng() {
    constexpr std::array<u8, 4> kRed{255, 0, 0, 255};
    return test::writePng(1, 1, test::PngColor::Rgba, kRed);
}

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

TEST_CASE("App: assets() loads images through the mounts", "[engine][app][assets]") {
    AssetsApp fixture{{}, [](test::MemoryFileSystem& files) {
                          files.addFile("/game/assets/hero.png", redPng());
                          files.addFile("/game/assets/coin.png", redPng());
                      }};
    REQUIRE(fixture.app.isInitialized());

    const assets::Asset<assets::ImageData> hero =
        fixture.app.assets().load<assets::ImageData>("assets://hero.png");

    REQUIRE(hero.isReady());
    CHECK(hero.get().width == 1);
    // Without a device, there is no texture loader.
    const assets::Asset<renderer::Texture> coin =
        fixture.app.assets().load<renderer::Texture>("assets://coin.png");
    CHECK_FALSE(coin.isReady());
}

TEST_CASE("App: with a device, assets() loads textures", "[engine][app][assets]") {
    AssetsApp fixture{{},
                      [](test::MemoryFileSystem& files) { files.addFile("/game/assets/hero.png", redPng()); },
                      rhi::Backend::Null};
    REQUIRE(fixture.app.isInitialized());

    const assets::Asset<renderer::Texture> hero =
        fixture.app.assets().load<renderer::Texture>("assets://hero.png");

    REQUIRE(hero.isReady());
    CHECK(hero.get().size() == UVec2{1, 1});
    const auto& device = static_cast<rhi::null::NullDevice&>(fixture.app.device());
    CHECK(device.texture(hero.get().handle()) != nullptr);
}

TEST_CASE("App: assets nothing refers to are unloaded at the end of the frame", "[engine][app][assets]") {
    AssetsApp fixture{
        {}, [](test::MemoryFileSystem& files) { files.addFile("/game/assets/hero.png", redPng()); }};
    REQUIRE(fixture.app.isInitialized());
    std::vector<usize> sizes;
    assets::Asset<assets::ImageData> hero;

    const int exitCode = fixture.app.run({.update = [&](App& app, f32) {
        sizes.push_back(app.assets().size());
        if (app.frameCount() == 0) {
            hero = app.assets().load<assets::ImageData>("assets://hero.png");
        } else if (app.frameCount() == 1) {
            hero = {};
            CHECK(app.assets().size() == 1); // unloaded when this frame ends, not at once
        } else {
            app.quit();
        }
    }});

    CHECK(exitCode == 0);
    CHECK(sizes == std::vector<usize>{0, 1, 0});
}
