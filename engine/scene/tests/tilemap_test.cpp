// Tilemap: chunked, sparse layers of tile ids with dirty tracking.

#include <teserra/scene/tilemap.hpp>

#include <teserra/core/math.hpp>
#include <teserra/ecs/world.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <set>
#include <utility>

using namespace teserra;
using namespace teserra::scene;

namespace {

// Builds the layer and marks every chunk as built, like the renderer would.
void clearAllDirty(TilemapLayer& layer) {
    std::set<std::pair<i32, i32>> coords;
    layer.forEachChunk([&](IVec2 coord, const TilemapChunk&) { coords.emplace(coord.x, coord.y); });
    for (const auto& [x, y] : coords) {
        layer.clearDirty({x, y});
    }
}

} // namespace

TEST_CASE("Tilemap: chunkOf() divides by the chunk size, rounding down", "[scene][tilemap]") {
    CHECK(chunkOf({0, 0}) == IVec2{0, 0});
    CHECK(chunkOf({31, 31}) == IVec2{0, 0});
    CHECK(chunkOf({32, 0}) == IVec2{1, 0});
    CHECK(chunkOf({-1, 0}) == IVec2{-1, 0});
    CHECK(chunkOf({-32, -33}) == IVec2{-1, -2});
    CHECK(chunkOf({-33, 63}) == IVec2{-2, 1});
    CHECK(chunkOf({64, -64}) == IVec2{2, -2});
}

TEST_CASE("Tilemap: tiles are set and read back, also at chunk borders and negative coordinates",
          "[scene][tilemap]") {
    TilemapLayer layer("ground");
    CHECK(layer.name() == "ground");
    const std::array coords = std::to_array<IVec2>(
        {{0, 0}, {31, 0}, {32, 0}, {-1, 0}, {0, -1}, {-1, -1}, {-32, -32}, {-33, 31}, {100, -77}});
    TileId next = 1;
    for (const IVec2 coord : coords) {
        layer.setTile(coord, next++);
    }
    next = 1;
    for (const IVec2 coord : coords) {
        CHECK(layer.tile(coord) == next++);
    }
    // Neighbors of the set tiles are empty.
    CHECK(layer.tile({1, 0}) == kNoTile);
    CHECK(layer.tile({-2, -1}) == kNoTile);
    CHECK(layer.tile({30, 0}) == kNoTile);
    // A tile stored in its chunk at the expected index: row 0 at the bottom.
    layer.setTile({3, 2}, 9);
    const TilemapChunk* chunk = layer.chunk({0, 0});
    REQUIRE(chunk != nullptr);
    CHECK(chunk->tiles[(2 * kTilemapChunkSize) + 3] == 9);
    const TilemapChunk* negative = layer.chunk({-1, -1});
    REQUIRE(negative != nullptr);
    CHECK(negative->tiles[(31 * kTilemapChunkSize) + 31] == 6); // tile (-1, -1)
}

TEST_CASE("Tilemap: sparse layers allocate only the chunks with tiles", "[scene][tilemap]") {
    TilemapLayer layer("sparse");
    CHECK(layer.chunkCount() == 0);
    CHECK(layer.tile({5, 5}) == kNoTile);
    CHECK(layer.chunk({0, 0}) == nullptr);

    layer.setTile({5, 5}, kNoTile); // kNoTile never allocates
    CHECK(layer.chunkCount() == 0);

    layer.setTile({0, 0}, 1);
    layer.setTile({31, 31}, 1);
    layer.setTile({10'000, -10'000}, 2);
    CHECK(layer.chunkCount() == 2);
    CHECK(layer.chunk({0, 0})->tileCount == 2);

    usize visited = 0;
    layer.forEachChunk([&](IVec2 coord, const TilemapChunk& chunk) {
        ++visited;
        CHECK(layer.chunk(coord) == &chunk);
    });
    CHECK(visited == 2);
}

TEST_CASE("Tilemap: an edit marks exactly its chunk dirty", "[scene][tilemap]") {
    TilemapLayer layer("ground");
    for (i32 y = -32; y < 40; y += 8) { // chunks -1, 0 and 1 on each axis
        for (i32 x = -32; x < 40; x += 8) {
            layer.setTile({x, y}, 1);
        }
    }
    CHECK(layer.chunkCount() == 9);
    CHECK(layer.dirtyChunkCount() == 9); // new chunks are dirty
    clearAllDirty(layer);
    CHECK(layer.dirtyChunkCount() == 0);

    SECTION("a changed tile") {
        layer.setTile({-1, -1}, 2);
        CHECK(layer.dirtyChunkCount() == 1);
        CHECK(layer.chunk({-1, -1})->dirty);
    }
    SECTION("the same value marks nothing") {
        layer.setTile({0, 0}, 1);
        layer.setTile({1, 1}, kNoTile);
        CHECK(layer.dirtyChunkCount() == 0);
    }
    SECTION("tiles on both sides of a chunk border") {
        layer.setTile({31, 5}, 3);
        CHECK(layer.dirtyChunkCount() == 1);
        layer.setTile({32, 5}, 3);
        CHECK(layer.dirtyChunkCount() == 2);
        CHECK(layer.chunk({0, 0})->dirty);
        CHECK(layer.chunk({1, 0})->dirty);
    }
    SECTION("a chunk emptied of its tiles stays allocated, dirty") {
        const TilemapChunk* chunk = layer.chunk({1, 1});
        REQUIRE(chunk != nullptr);
        CHECK(chunk->tileCount == 1); // tile (32, 32)
        layer.setTile({32, 32}, kNoTile);
        CHECK(chunk->tileCount == 0);
        CHECK(chunk->dirty);
        CHECK(layer.chunkCount() == 9);
    }
    SECTION("clearDirty() of an unallocated chunk does nothing") {
        layer.clearDirty({50, 50});
        CHECK(layer.chunkCount() == 9);
    }
}

TEST_CASE("Tilemap: layers are found by name and drawn in order", "[scene][tilemap]") {
    Tilemap map{.tileSize = {0.5F, 0.5F}};
    map.addLayer("ground").setTile({0, 0}, 1);
    map.addLayer("decor");
    REQUIRE(map.layers.size() == 2);
    CHECK(map.layers[0].name() == "ground");
    CHECK(map.layers[1].name() == "decor");

    TilemapLayer* ground = map.findLayer("ground");
    REQUIRE(ground != nullptr);
    CHECK(ground->tile({0, 0}) == 1);
    CHECK(map.findLayer("water") == nullptr);
    const Tilemap& constMap = map;
    CHECK(constMap.findLayer("decor") == &map.layers[1]);
}

TEST_CASE("Tilemap: is a component", "[scene][tilemap]") {
    ecs::World world;
    const ecs::Entity entity = world.create();
    Tilemap& map = world.add<Tilemap>(entity, Tilemap{.tileSize = {2.0F, 2.0F}});
    map.addLayer("ground").setTile({-5, 7}, 4);
    CHECK(world.get<Tilemap>(entity).layers.front().tile({-5, 7}) == 4);
    CHECK(ecs::componentName<Tilemap>() == "Tilemap");
}
