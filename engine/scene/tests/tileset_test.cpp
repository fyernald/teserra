// Tileset: tile ids to sprite sheet regions and tile properties.

#include <teserra/scene/tileset.hpp>

#include <teserra/assets/asset_registry.hpp>
#include <teserra/core/result.hpp>
#include <teserra/renderer/assets.hpp>
#include <teserra/renderer/sprite_sheet.hpp>
#include <teserra/rhi_null/null_device.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using namespace teserra;
using namespace teserra::scene;

namespace {

struct TextureFixture {
    std::unique_ptr<rhi::null::NullDevice> device = rhi::null::createHeadlessDevice({});
    assets::AssetRegistry registry;

    assets::Asset<renderer::Texture> texture(UVec2 size) {
        return registry.load<renderer::Texture>("game://tiles", [&]() -> Result<renderer::Texture> {
            TESERRA_TRY_ASSIGN(const rhi::TextureHandle handle,
                               device->createTexture({.width = size.x, .height = size.y}));
            return renderer::Texture(*device, handle, size);
        });
    }
};

} // namespace

TEST_CASE("Tileset: a grid tileset maps tile ids to cells, counting from 1", "[scene][tilemap]") {
    TextureFixture fixture;
    const Result<Tileset> made =
        Tileset::grid(fixture.texture({64, 32}), {.cellSize = {16, 16}}, {{}, {.solid = true}, {}});
    REQUIRE(made.has_value());
    const Tileset& tileset = made.value();
    CHECK(tileset.tileCount() == 8);
    CHECK_FALSE(tileset.contains(kNoTile));
    CHECK(tileset.contains(1));
    CHECK(tileset.contains(8));
    CHECK_FALSE(tileset.contains(9));

    CHECK(tileset.uv(1) == renderer::UvRect{.min = {0.0F, 0.0F}, .max = {0.25F, 0.5F}});
    CHECK(tileset.uv(2) == renderer::UvRect{.min = {0.25F, 0.0F}, .max = {0.5F, 0.5F}});
    CHECK(tileset.uv(5) == renderer::UvRect{.min = {0.0F, 0.5F}, .max = {0.25F, 1.0F}}); // second row
    CHECK(tileset.sheet().regions().size() == 8);
}

TEST_CASE("Tileset: properties default for empty, unknown and unlisted tiles", "[scene][tilemap]") {
    TextureFixture fixture;
    const Tileset tileset =
        Tileset::grid(fixture.texture({32, 16}), {.cellSize = {16, 16}}, {{.solid = true}}).value();
    CHECK(tileset.isSolid(1));
    CHECK_FALSE(tileset.isSolid(2)); // no properties given
    CHECK_FALSE(tileset.isSolid(kNoTile));
    CHECK_FALSE(tileset.isSolid(99));
    CHECK(tileset.properties(99) == TileProperties{});
}

TEST_CASE("Tileset: errors", "[scene][tilemap]") {
    TextureFixture fixture;
    const assets::Asset<renderer::Texture> texture = fixture.texture({32, 16});

    const Result<Tileset> tooMany =
        Tileset::grid(texture, {.cellSize = {16, 16}}, std::vector<TileProperties>(3));
    REQUIRE_FALSE(tooMany.has_value());
    CHECK(tooMany.error().code == ErrorCode::InvalidArgument);

    const Result<Tileset> zeroCells = Tileset::grid(texture, {.cellSize = {0, 16}});
    REQUIRE_FALSE(zeroCells.has_value());
    CHECK(zeroCells.error().code == ErrorCode::InvalidArgument);
}

#if TESERRA_ENABLE_ASSERTS

TEST_CASE("Tileset: uv() asserts for tiles it does not have", "[scene][tilemap][validation]") {
    TextureFixture fixture;
    const Tileset tileset = Tileset::grid(fixture.texture({32, 16}), {.cellSize = {16, 16}}).value();
    CHECK(test::requireAssert([&] { static_cast<void>(tileset.uv(kNoTile)); }).message() ==
          "Tile 0 is not in a tileset of 2 tiles");
    CHECK(test::requireAssert([&] { static_cast<void>(tileset.uv(3)); }).message() ==
          "Tile 3 is not in a tileset of 2 tiles");
}

#endif
