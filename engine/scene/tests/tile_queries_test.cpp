// Tile queries: world <-> tile coordinates, tileAt and tilesOverlapping, without allocations.

#include <teserra/scene/tile_queries.hpp>

#include <teserra/core/math.hpp>
#include <teserra/scene/tilemap.hpp>
#include <teserra/scene/transform.hpp>
#include <teserra/test/allocation_counter.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <utility>
#include <vector>

using namespace teserra;
using namespace teserra::scene;

namespace {

Mat4 identity() {
    return Mat4{1.0F};
}

Aabb box(Vec2 min, Vec2 max) {
    return {.min = {min.x, min.y, 0.0F}, .max = {max.x, max.y, 0.0F}};
}

std::vector<std::pair<IVec2, TileId>> collect(const TilemapQuery& query, const TilemapLayer& layer,
                                              const Aabb& worldBox) {
    std::vector<std::pair<IVec2, TileId>> tiles;
    query.tilesOverlapping(layer, worldBox,
                           [&](IVec2 coord, TileId tile) { tiles.emplace_back(coord, tile); });
    return tiles;
}

} // namespace

TEST_CASE("Tile queries: world positions map to tiles by floor, also negative ones", "[scene][tilemap]") {
    const Tilemap map{.tileSize = {2.0F, 0.5F}, .layers = {}};
    CHECK(localToTile(map, {0.0F, 0.0F}) == IVec2{0, 0});
    CHECK(localToTile(map, {1.99F, 0.49F}) == IVec2{0, 0});
    CHECK(localToTile(map, {2.0F, 0.5F}) == IVec2{1, 1});
    CHECK(localToTile(map, {-0.01F, -0.01F}) == IVec2{-1, -1});
    CHECK(localToTile(map, {-2.0F, -0.5F}) == IVec2{-1, -1});
    CHECK(localToTile(map, {-2.01F, -0.51F}) == IVec2{-2, -2});

    const TilemapQuery query(map, identity());
    CHECK(query.worldToTile({65.0F, -16.1F, 3.0F}) == IVec2{32, -33}); // Z is ignored
}

TEST_CASE("Tile queries: the map's transform places its tiles in the world", "[scene][tilemap]") {
    Tilemap map{.tileSize = {1.0F, 1.0F}, .layers = {}};
    TilemapLayer& ground = map.addLayer("ground");
    ground.setTile({0, 0}, 1);
    ground.setTile({-1, 2}, 2);

    SECTION("translated and scaled") {
        const Transform placement{.position = {10.0F, 20.0F, 0.0F}, .scale = {2.0F, 2.0F, 1.0F}};
        const TilemapQuery query(map, placement.matrix());
        CHECK(query.worldToTile({10.5F, 20.5F, 0.0F}) == IVec2{0, 0});
        CHECK(query.worldToTile({9.9F, 24.1F, 0.0F}) == IVec2{-1, 2});
        CHECK(query.tileAt(ground, {11.9F, 21.9F, 0.0F}) == 1);
        CHECK(query.tileAt(ground, {12.1F, 20.0F, 0.0F}) == kNoTile);
        CHECK(query.tileAt(ground, {9.0F, 25.0F, 0.0F}) == 2);
        CHECK(query.tileCenter({0, 0}) == Vec3{11.0F, 21.0F, 0.0F});
        CHECK(query.tileBounds({-1, 2}) == Aabb{.min = {8.0F, 24.0F, 0.0F}, .max = {10.0F, 26.0F, 0.0F}});
    }
    SECTION("rotated a quarter turn") {
        Transform placement;
        placement.setRotation2D(math::radians(90.0F)); // local +X points to world +Y
        const TilemapQuery query(map, placement.matrix());
        CHECK(query.worldToTile({-0.5F, 0.5F, 0.0F}) == IVec2{0, 0});
        CHECK(query.tileAt(ground, {-2.5F, -0.5F, 0.0F}) == 2); // local (-0.5, 2.5)
        const Aabb bounds = query.tileBounds({0, 0});
        CHECK(bounds.min.x == Catch::Approx(-1.0F));
        CHECK(bounds.max.y == Catch::Approx(1.0F));
    }
}

TEST_CASE("Tile queries: tileRange() takes the tiles a box overlaps; edges only touch", "[scene][tilemap]") {
    const Tilemap map{.tileSize = {1.0F, 1.0F}, .layers = {}};
    const TilemapQuery query(map, identity());
    using Range = std::array<IVec2, 2>;
    CHECK(query.tileRange(box({0.2F, 0.2F}, {0.8F, 0.8F})) == Range{IVec2{0, 0}, IVec2{0, 0}});
    CHECK(query.tileRange(box({0.5F, 0.5F}, {2.5F, 1.5F})) == Range{IVec2{0, 0}, IVec2{2, 1}});
    // Exactly one tile: the right and top edges touch the next tiles without reaching them.
    CHECK(query.tileRange(box({0.0F, 0.0F}, {1.0F, 1.0F})) == Range{IVec2{0, 0}, IVec2{0, 0}});
    CHECK(query.tileRange(box({-1.5F, -0.5F}, {-0.5F, 0.5F})) == Range{IVec2{-2, -1}, IVec2{-1, 0}});
    // A point gets its tile, like worldToTile().
    CHECK(query.tileRange(box({3.0F, -2.0F}, {3.0F, -2.0F})) == Range{IVec2{3, -2}, IVec2{3, -2}});
}

TEST_CASE("Tile queries: tilesOverlapping() visits the set tiles across chunk borders", "[scene][tilemap]") {
    Tilemap map{.tileSize = {1.0F, 1.0F}, .layers = {}};
    TilemapLayer& layer = map.addLayer("ground");
    layer.setTile({-1, -1}, 1);
    layer.setTile({0, -1}, 2);
    layer.setTile({-1, 0}, 3);
    layer.setTile({31, 0}, 4);
    layer.setTile({32, 0}, 5);
    layer.setTile({0, 5}, 6); // outside the boxes below
    const TilemapQuery query(map, identity());

    // The four chunks around the origin, row by row from the bottom left.
    CHECK(collect(query, layer, box({-1.5F, -1.5F}, {0.5F, 0.5F})) ==
          std::vector<std::pair<IVec2, TileId>>{{{-1, -1}, 1}, {{0, -1}, 2}, {{-1, 0}, 3}});
    CHECK(collect(query, layer, box({31.2F, 0.2F}, {32.8F, 0.8F})) ==
          std::vector<std::pair<IVec2, TileId>>{{{31, 0}, 4}, {{32, 0}, 5}});
    CHECK(collect(query, layer, box({100.0F, 100.0F}, {200.0F, 200.0F})).empty());

    CHECK(query.anyOverlapping(layer, box({30.5F, 0.0F}, {31.5F, 0.5F}),
                               [](TileId tile) { return tile == 4; }));
    CHECK_FALSE(query.anyOverlapping(layer, box({-1.5F, -1.5F}, {0.5F, 0.5F}),
                                     [](TileId tile) { return tile == 4; }));
}

TEST_CASE("Tile queries: do not allocate", "[scene][tilemap]") {
    Tilemap map{.tileSize = {1.0F, 1.0F}, .layers = {}};
    TilemapLayer& layer = map.addLayer("ground");
    for (i32 x = -40; x < 40; ++x) {
        layer.setTile({x, 0}, 1);
    }
    const Mat4 world = Transform{.position = {3.0F, 4.0F, 0.0F}}.matrix();

    const test::AllocationCounter allocations;
    const TilemapQuery query(map, world);
    u32 visited = 0;
    query.tilesOverlapping(layer, box({-50.0F, 3.0F}, {50.0F, 5.0F}), [&](IVec2, TileId) { ++visited; });
    const TileId tile = query.tileAt(layer, {3.5F, 4.5F, 0.0F});
    const bool any =
        query.anyOverlapping(layer, box({0.0F, 0.0F}, {10.0F, 10.0F}), [](TileId t) { return t == 1; });
    CHECK(allocations.count() == 0);
    CHECK(visited == 80);
    CHECK(tile == 1);
    CHECK(any);
}
