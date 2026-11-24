// Sprite sheet files: a grid sheet and a named-region sheet load from .sheet.json through assets:// and give
// the same regions as in code; a sheet whose texture is missing still loads, with the fallback; malformed
// sheets fail once, with the path and the reason.

#include <teserra/assets/asset_registry.hpp>
#include <teserra/assets/mount_table.hpp>
#include <teserra/core/log.hpp>
#include <teserra/renderer/assets.hpp>
#include <teserra/renderer/sprite_sheet.hpp>
#include <teserra/rhi_null/null_device.hpp>
#include <teserra/test/log_capture.hpp>
#include <teserra/test/memory_file_system.hpp>
#include <teserra/test/png.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace teserra;
using namespace teserra::renderer;
using Catch::Matchers::ContainsSubstring;

namespace {

// assets:// over files in memory, with the renderer's loaders on a null device and a 64x32 PNG at
// assets://sprites/characters.png.
struct Fixture {
    Fixture() {
        REQUIRE(mounts.mount("assets", "/game/assets"));
        const std::vector<u8> texels(usize{64} * 32 * 4, 255);
        files.addFile("/game/assets/sprites/characters.png",
                      test::writePng(64, 32, test::PngColor::Rgba, texels));
        REQUIRE(registerAssetLoaders(registry, *device));
    }

    void addSheet(std::string_view name, std::string_view json) {
        files.addTextFile("/game/assets/sprites/" + std::string(name), json);
    }

    test::MemoryFileSystem files;
    assets::MountTable mounts{files};
    std::unique_ptr<rhi::null::NullDevice> device = rhi::null::createHeadlessDevice({});
    assets::AssetRegistry registry{{.mounts = &mounts}};
};

usize errorsMentioning(const test::LogCapture& logs, std::string_view text) {
    return static_cast<usize>(std::ranges::count_if(logs.records(), [&](const test::CapturedLog& record) {
        return record.level == LogLevel::Error && record.message.find(text) != std::string::npos;
    }));
}

} // namespace

TEST_CASE("Sprite sheet files: a grid sheet gives the regions of the grid in code",
          "[renderer][sprite_sheet]") {
    Fixture fixture;
    fixture.addSheet("heroes.sheet.json", R"({
        "texture": "characters.png",
        "pixelsPerUnit": 32,
        "grid": {"cellSize": [16, 16], "margin": [0, 0], "spacing": [0, 0], "count": 6, "namePrefix": "hero"}
    })");

    const assets::Asset<SpriteSheet> sheet =
        fixture.registry.load<SpriteSheet>("assets://sprites/heroes.sheet.json");
    REQUIRE(sheet.isLoaded());
    const assets::Asset<Texture> texture = fixture.registry.load<Texture>("assets://sprites/characters.png");
    const SpriteSheet inCode =
        SpriteSheet::grid(texture, {.cellSize = {16, 16}, .count = 6, .namePrefix = "hero"}, 32.0F).value();

    CHECK(std::ranges::equal(sheet.get().regions(), inCode.regions()));
    CHECK(sheet.get().texture().id() == texture.id()); // the texture, loaded once
    CHECK(sheet.get().pixelsPerUnit() == 32.0F);
    CHECK(sheet.get().find("hero5") == 5);
    CHECK(sheet.get().worldSize(0) == Vec2{0.5F, 0.5F});
}

TEST_CASE("Sprite sheet files: a named-region sheet gives the regions in code", "[renderer][sprite_sheet]") {
    Fixture fixture;
    fixture.addSheet("named.sheet.json", R"({
        "texture": "assets://sprites/characters.png",
        "regions": [
            {"name": "idle", "position": [0, 0], "size": [16, 32]},
            {"name": "coin", "position": [48, 16], "size": [16, 16]}
        ]
    })");

    const assets::Asset<SpriteSheet> sheet =
        fixture.registry.load<SpriteSheet>("assets://sprites/named.sheet.json");
    REQUIRE(sheet.isLoaded());
    const std::vector<SpriteRegion> expected{{.name = "idle", .position = {0, 0}, .size = {16, 32}},
                                             {.name = "coin", .position = {48, 16}, .size = {16, 16}}};
    CHECK(std::ranges::equal(sheet.get().regions(), expected));
    CHECK(sheet.get().pixelsPerUnit() == kDefaultPixelsPerUnit);
    CHECK(sheet.get().uv(1) == UvRect{.min = {0.75F, 0.5F}, .max = {1.0F, 1.0F}});
}

TEST_CASE("Sprite sheet files: a missing texture loads the sheet with the fallback",
          "[renderer][sprite_sheet]") {
    Fixture fixture;
    fixture.addSheet("lost.sheet.json", R"({
        "texture": "missing.png",
        "grid": {"cellSize": [16, 16], "count": 3}
    })");
    const test::LogCapture logs;
    const assets::Asset<SpriteSheet> sheet =
        fixture.registry.load<SpriteSheet>("assets://sprites/lost.sheet.json");
    REQUIRE(sheet.isLoaded());
    CHECK(sheet.get().texture().isFallback());
    CHECK(sheet.get().regions().size() == 3);
    CHECK(sheet.get().uv(2) == UvRect{}); // the whole fallback
    CHECK(errorsMentioning(logs, "assets://sprites/missing.png") == 1);
}

TEST_CASE("Sprite sheet files: malformed sheets fail once, with the path and the reason",
          "[renderer][sprite_sheet]") {
    const auto failure = [](std::string_view json, std::string_view reason) {
        Fixture fixture;
        fixture.addSheet("bad.sheet.json", json);
        const test::LogCapture logs;
        const assets::Asset<SpriteSheet> sheet =
            fixture.registry.load<SpriteSheet>("assets://sprites/bad.sheet.json");
        CHECK_FALSE(sheet.isReady());
        CHECK(errorsMentioning(logs, "assets://sprites/bad.sheet.json") == 1);
        CHECK(errorsMentioning(logs, reason) == 1);
        // Loading it again does not log again.
        const assets::Asset<SpriteSheet> again =
            fixture.registry.load<SpriteSheet>("assets://sprites/bad.sheet.json");
        CHECK(errorsMentioning(logs, "assets://sprites/bad.sheet.json") == 1);
    };
    failure(R"({"texture": "characters.png", "grid": {"cellSize": [16, 16]},)", "line 1, column ");
    failure(R"({"grid": {"cellSize": [16, 16]}})", "'texture' must be a string, not missing");
    failure(R"({"texture": "characters.png"})", "either 'grid' or 'regions'");
    failure(R"({"texture": "characters.png", "grid": {"cellSize": [16]}, "regions": []})",
            "either 'grid' or 'regions'");
    failure(R"({"texture": "characters.png", "grid": {"cellSize": [16]}})",
            "grid: 'cellSize' must be an array of two non-negative integers");
    failure(R"({"texture": "characters.png", "grid": {"cellSize": [16, 16], "count": 9}})",
            "grid: A 64x32 texture has room for 8 cells");
    failure(R"({"texture": "characters.png", "pixelsPerUnit": 0, "grid": {"cellSize": [16, 16]}})",
            "'pixelsPerUnit' must be a positive number");
    failure(
        R"({"texture": "characters.png", "regions": [{"name": "big", "position": [60, 0], "size": [16, 16]}]})",
        "region 'big' (60, 0; 16x16) is outside the 64x32 texture");
    failure(R"({"texture": "characters.png", "regions": [{"position": [0, 0], "size": [16, 16]}]})",
            "region 0: 'name' must be a string");
    failure(R"({"texture": "../../escape.png", "regions": []})", "leaves the root of the mount point");
}
