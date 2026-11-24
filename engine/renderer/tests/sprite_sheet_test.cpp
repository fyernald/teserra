// Sprite sheets: region UVs with the top-left origin, grids (margin, spacing, count, names), lookups, world
// sizes from pixelsPerUnit, and sheets of fallback textures.

#include <teserra/renderer/sprite_sheet.hpp>

#include <teserra/assets/asset_registry.hpp>
#include <teserra/core/result.hpp>
#include <teserra/rhi_null/null_device.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace teserra;
using namespace teserra::renderer;
using Catch::Matchers::ContainsSubstring;

namespace {

// Textures of chosen sizes in a registry, and a missing one that resolves to the fallback.
struct Fixture {
    Fixture() { REQUIRE(registerAssetLoaders(registry, *device)); }

    assets::Asset<Texture> texture(std::string_view path, UVec2 size) {
        return registry.load<Texture>(path, [&]() -> Result<Texture> {
            TESERRA_TRY_ASSIGN(const rhi::TextureHandle handle,
                               device->createTexture({.width = size.x, .height = size.y}));
            return Texture(*device, handle, size);
        });
    }

    assets::Asset<Texture> missingTexture() {
        return registry.load<Texture>("assets://missing.png", []() -> Result<Texture> {
            return fail(ErrorCode::NotFound, Module::Game, "missing");
        });
    }

    std::unique_ptr<rhi::null::NullDevice> device = rhi::null::createHeadlessDevice({});
    assets::AssetRegistry registry;
};

} // namespace

TEST_CASE("Sprite sheets: regionUv() uses the texture's top-left corner as origin",
          "[renderer][sprite_sheet]") {
    CHECK(regionUv({64, 32}, {0, 0}, {64, 32}) == UvRect{.min = Vec2{0.0F}, .max = Vec2{1.0F}});
    CHECK(regionUv({64, 32}, {0, 0}, {16, 16}) == UvRect{.min = {0.0F, 0.0F}, .max = {0.25F, 0.5F}});
    // The bottom-right cell: larger v is further down.
    CHECK(regionUv({64, 32}, {48, 16}, {16, 16}) == UvRect{.min = {0.75F, 0.5F}, .max = {1.0F, 1.0F}});
    CHECK(regionUv({10, 10}, {1, 2}, {3, 4}) == UvRect{.min = {0.1F, 0.2F}, .max = {0.4F, 0.6F}});
}

TEST_CASE("Sprite sheets: named regions in code", "[renderer][sprite_sheet]") {
    Fixture fixture;
    const SpriteSheet sheet(fixture.texture("game://items", {32, 16}),
                            {{.name = "coin", .position = {0, 0}, .size = {8, 8}},
                             {.name = "gem", .position = {8, 0}, .size = {8, 16}},
                             {.name = "key", .position = {16, 8}, .size = {16, 8}}},
                            8.0F);

    REQUIRE(sheet.regions().size() == 3);
    CHECK(sheet.find("coin") == 0U);
    CHECK(sheet.find("key") == 2U);
    CHECK_FALSE(sheet.find("Coin").has_value());
    CHECK(sheet.uv(1) == UvRect{.min = {0.25F, 0.0F}, .max = {0.5F, 1.0F}});
    CHECK(sheet.uv(2) == UvRect{.min = {0.5F, 0.5F}, .max = {1.0F, 1.0F}});
    CHECK(sheet.pixelsPerUnit() == 8.0F);
    CHECK(sheet.worldSize(1) == Vec2{1.0F, 2.0F});
    CHECK(sheet.worldSize(2) == Vec2{2.0F, 1.0F});
}

TEST_CASE("Sprite sheets: a grid takes every whole cell, row by row from the top left",
          "[renderer][sprite_sheet]") {
    Fixture fixture;
    // 3 columns and 2 rows fit; the last 4 texels of each row and column are partial cells.
    const Result<SpriteSheet> sheet = SpriteSheet::grid(fixture.texture("game://grid", {52, 36}),
                                                        {.cellSize = {16, 16}, .namePrefix = "tile"});

    REQUIRE(sheet.has_value());
    REQUIRE(sheet->regions().size() == 6);
    CHECK(sheet->regions()[0] == SpriteRegion{.name = "tile0", .position = {0, 0}, .size = {16, 16}});
    CHECK(sheet->regions()[2] == SpriteRegion{.name = "tile2", .position = {32, 0}, .size = {16, 16}});
    CHECK(sheet->regions()[3] == SpriteRegion{.name = "tile3", .position = {0, 16}, .size = {16, 16}});
    CHECK(sheet->regions()[5] == SpriteRegion{.name = "tile5", .position = {32, 16}, .size = {16, 16}});
    CHECK(sheet->find("tile4") == 4U);
    CHECK(sheet->pixelsPerUnit() == kDefaultPixelsPerUnit);
    CHECK(sheet->worldSize(0) == Vec2{1.0F});
}

TEST_CASE("Sprite sheets: grids with margin, spacing and a count", "[renderer][sprite_sheet]") {
    Fixture fixture;
    // margin 2, cells 8, spacing 1: x = 2, 11, 20, 29 (29 + 8 = 37 <= 40); y = 2, 11 (11 + 8 = 19 <= 20).
    const assets::Asset<Texture> texture = fixture.texture("game://spaced", {40, 20});

    const Result<SpriteSheet> all =
        SpriteSheet::grid(texture, {.cellSize = {8, 8}, .margin = {2, 2}, .spacing = {1, 1}});
    REQUIRE(all.has_value());
    REQUIRE(all->regions().size() == 8);
    CHECK(all->regions()[3].position == UVec2{29, 2});
    CHECK(all->regions()[4].position == UVec2{2, 11});
    CHECK(all->regions()[0].name == "0");

    const Result<SpriteSheet> five = SpriteSheet::grid(
        texture, {.cellSize = {8, 8}, .margin = {2, 2}, .spacing = {1, 1}, .count = 5}, 32.0F);
    REQUIRE(five.has_value());
    CHECK(five->regions().size() == 5);
    CHECK(five->regions()[4].position == UVec2{2, 11});
    CHECK(five->worldSize(4) == Vec2{0.25F});
}

TEST_CASE("Sprite sheets: invalid grids are errors", "[renderer][sprite_sheet]") {
    Fixture fixture;
    const assets::Asset<Texture> texture = fixture.texture("game://small", {16, 16});

    const Result<SpriteSheet> zeroCell = SpriteSheet::grid(texture, {.cellSize = {0, 8}});
    REQUIRE_FALSE(zeroCell.has_value());
    CHECK(zeroCell.error().code == ErrorCode::InvalidArgument);

    const Result<SpriteSheet> tooMany = SpriteSheet::grid(texture, {.cellSize = {8, 8}, .count = 5});
    REQUIRE_FALSE(tooMany.has_value());
    CHECK(tooMany.error().code == ErrorCode::InvalidArgument);
    CHECK_THAT(tooMany.error().message, ContainsSubstring("room for 4 cells"));

    const Result<SpriteSheet> tooLarge = SpriteSheet::grid(texture, {.cellSize = {32, 32}, .count = 1});
    REQUIRE_FALSE(tooLarge.has_value());

    const Result<SpriteSheet> noPixels = SpriteSheet::grid(texture, {.cellSize = {8, 8}}, 0.0F);
    REQUIRE_FALSE(noPixels.has_value());
    CHECK(noPixels.error().code == ErrorCode::InvalidArgument);

    const Result<SpriteSheet> noTexture = SpriteSheet::grid({}, {.cellSize = {8, 8}});
    REQUIRE_FALSE(noTexture.has_value());
    CHECK(noTexture.error().code == ErrorCode::NotFound);
}

TEST_CASE("Sprite sheets: a fallback texture shows whole in every region", "[renderer][sprite_sheet]") {
    Fixture fixture;
    const assets::Asset<Texture> missing = fixture.missingTexture();
    REQUIRE(missing.isFallback());

    const Result<SpriteSheet> grid =
        SpriteSheet::grid(missing, {.cellSize = {16, 16}, .count = 6, .namePrefix = "hero"});
    REQUIRE(grid.has_value());
    CHECK(grid->regions().size() == 6);
    CHECK(grid->find("hero5") == 5U);
    CHECK(grid->uv(5) == UvRect{});
    CHECK(grid->worldSize(5) == Vec2{1.0F}); // the size the art would have had

    const Result<SpriteSheet> unknownCount = SpriteSheet::grid(missing, {.cellSize = {16, 16}});
    REQUIRE(unknownCount.has_value());
    CHECK(unknownCount->regions().size() == 1);

    const SpriteSheet named(missing, {{.name = "coin", .position = {8, 8}, .size = {8, 8}}});
    CHECK(named.uv(0) == UvRect{});
}

TEST_CASE("Sprite sheets: a sheet keeps its texture loaded", "[renderer][sprite_sheet]") {
    Fixture fixture;
    std::optional<SpriteSheet> sheet;
    {
        sheet = *SpriteSheet::grid(fixture.texture("game://kept", {16, 16}), {.cellSize = {8, 8}});
    }
    fixture.registry.endFrame();
    CHECK(fixture.registry.size() == 1);
    CHECK(sheet->texture().isLoaded());

    sheet.reset();
    fixture.registry.endFrame();
    CHECK(fixture.registry.size() == 0);
}
