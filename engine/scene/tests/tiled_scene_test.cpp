// Tiled maps in a scene: tile layers become a Tilemap with rows flipped to +Y up, the tileset becomes a
// Tileset with "solid" tiles, and object rectangles land in the map's local space.

#include <teserra/assets/asset_registry.hpp>
#include <teserra/assets/mount_table.hpp>
#include <teserra/assets/tiled.hpp>
#include <teserra/renderer/assets.hpp>
#include <teserra/rhi_null/null_device.hpp>
#include <teserra/scene/tiled.hpp>
#include <teserra/test/memory_file_system.hpp>
#include <teserra/test/png.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace teserra;
using namespace teserra::scene;
using Catch::Matchers::ContainsSubstring;

namespace {

// A 3x2 map of 16-pixel tiles: tileset "terrain" (gids 1-4, tile 1 solid) and "props" (gids 5-6).
assets::TiledMap twoRowMap() {
    assets::TiledMap map{.size = {3, 2}, .tileSize = {16, 16}};
    map.tilesets.push_back({.firstGid = 1,
                            .name = "terrain",
                            .image = "assets://tiles/terrain.png",
                            .imageSize = {32, 32},
                            .tileSize = {16, 16},
                            .columns = 2,
                            .tileCount = 4,
                            .tiles = {{.id = 1, .properties = {{{.name = "solid", .value = true}}}},
                                      {.id = 2, .properties = {{{.name = "solid", .value = false}}}}}});
    map.tilesets.push_back(
        {.firstGid = 5, .name = "props", .image = "assets://tiles/props.png", .tileCount = 2});
    // Top row first, as Tiled stores it.
    map.tileLayers.push_back({.name = "ground", .size = {3, 2}, .gids = {1, 2, 0, 3, 4, 4}});
    return map;
}

} // namespace

TEST_CASE("Tiled in a scene: tile layers become a Tilemap with the bottom row at y = 0", "[scene][tiled]") {
    const Result<Tilemap> converted = tilemapFromTiled(twoRowMap());
    REQUIRE(converted.has_value());
    const Tilemap& tilemap = converted.value();
    CHECK(tilemap.tileSize == Vec2{1.0F, 1.0F}); // 16 pixels at 16 pixels per unit
    REQUIRE(tilemap.layers.size() == 1);
    const TilemapLayer& ground = tilemap.layers[0];
    CHECK(ground.name() == "ground");
    // Tiled's bottom row (3, 4, 4) is y = 0, its top row (1, 2, -) is y = 1.
    CHECK(ground.tile({0, 0}) == 3);
    CHECK(ground.tile({1, 0}) == 4);
    CHECK(ground.tile({2, 0}) == 4);
    CHECK(ground.tile({0, 1}) == 1);
    CHECK(ground.tile({1, 1}) == 2);
    CHECK(ground.tile({2, 1}) == kNoTile);

    CHECK(tilemapFromTiled(twoRowMap(), 0, 32.0F).value().tileSize == Vec2{0.5F, 0.5F});
}

TEST_CASE("Tiled in a scene: tiles of another tileset and bad arguments fail", "[scene][tiled]") {
    assets::TiledMap map = twoRowMap();
    map.tileLayers[0].gids[2] = 6; // a prop at Tiled tile (2, 0)
    const Result<Tilemap> mixed = tilemapFromTiled(map);
    REQUIRE_FALSE(mixed.has_value());
    CHECK(mixed.error().code == ErrorCode::Unsupported);
    CHECK_THAT(mixed.error().message,
               ContainsSubstring("Layer 'ground': tile (2, 0) is from tileset 'props'"));

    // The props tileset alone: its ids count from 1 again, and terrain tiles fail.
    assets::TiledMap props = twoRowMap();
    props.tileLayers[0].gids = {5, 6, 0, 0, 0, 5};
    const Tilemap propsMap = tilemapFromTiled(props, 1).value();
    CHECK(propsMap.layers[0].tile({0, 1}) == 1);
    CHECK(propsMap.layers[0].tile({1, 1}) == 2);
    CHECK_FALSE(tilemapFromTiled(twoRowMap(), 1).has_value());

    CHECK(tilemapFromTiled(twoRowMap(), 2).error().code == ErrorCode::InvalidArgument);
    CHECK(tilemapFromTiled(twoRowMap(), 0, 0.0F).error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("Tiled in a scene: the tileset becomes a Tileset with solid tiles", "[scene][tiled]") {
    test::MemoryFileSystem files;
    const std::vector<u8> texels(usize{34} * 34 * 4, 200);
    files.addFile("/game/assets/tiles/terrain.png", test::writePng(34, 34, test::PngColor::Rgba, texels));
    assets::MountTable mounts{files};
    REQUIRE(mounts.mount("assets", "/game/assets"));
    const std::unique_ptr<rhi::null::NullDevice> device = rhi::null::createHeadlessDevice({});
    assets::AssetRegistry registry{{.mounts = &mounts}};
    REQUIRE(renderer::registerAssetLoaders(registry, *device));

    assets::TiledTileset source = twoRowMap().tilesets[0];
    source.margin = 1;
    source.spacing = 0;
    const Result<Tileset> tileset = tilesetFromTiled(source, registry);
    REQUIRE(tileset.has_value());
    CHECK(tileset->tileCount() == 4);
    CHECK_FALSE(tileset->isSolid(1));
    CHECK(tileset->isSolid(2)); // Tiled tile id 1
    CHECK_FALSE(tileset->isSolid(3));
    CHECK(tileset->sheet().texture().isLoaded());
    // The second cell starts after the 1-pixel margin and one 16-pixel tile.
    CHECK(tileset->sheet().regions()[1].position == UVec2{17, 1});
}

TEST_CASE("Tiled in a scene: object rectangles in the map's local space", "[scene][tiled]") {
    const assets::TiledMap map = twoRowMap(); // 32 pixels tall
    const assets::TiledObject spawn{.position = {8.0F, 4.0F}, .size = {16.0F, 8.0F}};
    const Aabb bounds = tiledObjectBounds(map, spawn);
    CHECK(bounds.min == Vec3{0.5F, 1.25F, 0.0F}); // y: (32 - 4 - 8) / 16
    CHECK(bounds.max == Vec3{1.5F, 1.75F, 0.0F});
}
