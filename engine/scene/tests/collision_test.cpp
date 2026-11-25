// Collision: boxes move against solid tiles without tunneling (scripted movement at up to and beyond one tile
// per frame), and overlap events fire when colliders start and stop overlapping.

#include <teserra/scene/collision.hpp>

#include <teserra/core/math.hpp>
#include <teserra/ecs/events.hpp>
#include <teserra/scene/scene.hpp>
#include <teserra/scene/tile_queries.hpp>
#include <teserra/scene/tilemap.hpp>
#include <teserra/scene/transform.hpp>
#include <teserra/test/allocation_counter.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace teserra;
using namespace teserra::scene;
using Catch::Approx;

namespace {

constexpr TileId kWall = 1;

bool solid(TileId tile) {
    return tile == kWall;
}

// A floor along y = -1 (tiles cover [-1, 0)) and a wall at x = 5 from y = 0 to 4.
struct Level {
    Level() {
        TilemapLayer& layer = map.addLayer("walls");
        for (i32 x = -20; x < 20; ++x) {
            layer.setTile({x, -1}, kWall);
        }
        for (i32 y = 0; y < 5; ++y) {
            layer.setTile({5, y}, kWall);
        }
    }
    [[nodiscard]] const TilemapLayer& walls() const { return map.layers[0]; }

    Tilemap map{.tileSize = {1.0F, 1.0F}};
};

Aabb unitBoxAt(f32 x, f32 y) {
    return colliderBounds(Collider{.size = {0.8F, 0.8F}}, {x, y, 0.0F});
}

} // namespace

TEST_CASE("Collision: a box stops flush against a wall, at any speed", "[scene][collision]") {
    Level level;
    const TilemapQuery query(level.map, Mat4{1.0F});
    for (const f32 speed : {0.3F, 1.0F, 3.0F, 50.0F}) {
        // From x = 0 towards the wall at [5, 6), scripted frame by frame.
        Aabb box = unitBoxAt(0.0F, 1.5F);
        for (int frame = 0; frame < 40; ++frame) {
            const MoveResult result = moveAgainstTiles(query, level.walls(), solid, box, {speed, 0.0F});
            box.min.x += result.moved.x;
            box.max.x += result.moved.x;
        }
        CHECK(box.max.x == Approx(5.0F)); // never inside or past the wall
    }
}

TEST_CASE("Collision: falling boxes land on the floor and rest without sinking", "[scene][collision]") {
    Level level;
    const TilemapQuery query(level.map, Mat4{1.0F});
    Aabb box = unitBoxAt(0.0F, 10.0F);
    bool landed = false;
    for (int frame = 0; frame < 30; ++frame) {
        const MoveResult result =
            moveAgainstTiles(query, level.walls(), solid, box, {0.0F, -1.0F}); // one tile
        box.min.y += result.moved.y;
        box.max.y += result.moved.y;
        landed = landed || result.hitY;
    }
    CHECK(landed);
    CHECK(box.min.y == Approx(0.0F));
    // Resting on the floor, it slides freely along it.
    const MoveResult slide = moveAgainstTiles(query, level.walls(), solid, box, {2.0F, 0.0F});
    CHECK(slide.moved.x == Approx(2.0F));
    CHECK_FALSE(slide.hitX);
}

TEST_CASE("Collision: moving left and down, diagonally, and out of a wall", "[scene][collision]") {
    Level level;
    const TilemapQuery query(level.map, Mat4{1.0F});
    // From the right of the wall, moving left: stops at its right side (x = 6).
    const MoveResult left =
        moveAgainstTiles(query, level.walls(), solid, unitBoxAt(8.0F, 2.5F), {-5.0F, 0.0F});
    CHECK(left.hitX);
    CHECK(left.moved.x == Approx(-(8.0F - 0.4F - 6.0F)));
    // Diagonal into the corner of floor and wall: both axes stop.
    const MoveResult corner =
        moveAgainstTiles(query, level.walls(), solid, unitBoxAt(3.0F, 3.0F), {3.0F, -5.0F});
    CHECK(corner.hitX);
    CHECK(corner.hitY);
    CHECK(corner.moved.x == Approx(5.0F - 3.4F));
    CHECK(corner.moved.y == Approx(-(3.0F - 0.4F)));
    // A box that starts inside the wall can leave it.
    const MoveResult out = moveAgainstTiles(query, level.walls(), solid, unitBoxAt(5.5F, 2.5F), {2.0F, 0.0F});
    CHECK(out.moved.x == Approx(2.0F));
}

TEST_CASE("Collision: overlap events fire on enter and on exit", "[scene][collision]") {
    Scene level("level");
    const auto spawnAt = [&](f32 x) {
        const ecs::Entity entity = level.spawn();
        level.add<Transform>(entity, Transform{.position = {x, 0.0F, 0.0F}});
        level.add<Collider>(entity, Collider{.size = {1.0F, 1.0F}});
        return entity;
    };
    const ecs::Entity player = spawnAt(0.0F);
    const ecs::Entity coin = spawnAt(3.0F);
    const ecs::Entity spike = spawnAt(-3.0F);

    ecs::EventCursor startedCursor;
    ecs::EventCursor endedCursor;
    std::vector<std::pair<ecs::Entity, ecs::Entity>> started;
    std::vector<std::pair<ecs::Entity, ecs::Entity>> ended;
    const auto frame = [&] {
        started.clear();
        ended.clear();
        level.run(ecs::Phase::PreUpdate);
        level.run(ecs::Phase::PostUpdate);
        level.world().resource<ecs::Events<CollisionStarted>>().read(
            startedCursor, [&](const CollisionStarted& event) { started.emplace_back(event.a, event.b); });
        level.world().resource<ecs::Events<CollisionEnded>>().read(
            endedCursor, [&](const CollisionEnded& event) { ended.emplace_back(event.a, event.b); });
    };
    frame();
    CHECK(started.empty());

    level.world().get<Transform>(player).position.x = 2.5F; // into the coin
    frame();
    REQUIRE(started.size() == 1);
    CHECK(((started[0] == std::pair(player, coin)) || (started[0] == std::pair(coin, player))));
    CHECK(ended.empty());
    frame(); // still overlapping: nothing new
    CHECK(started.empty());

    level.world().get<Transform>(player).position.x = 2.0F; // touching edges only: not overlapping
    frame();
    CHECK(ended.size() == 1);

    level.world().get<Transform>(player).position.x = -2.8F; // onto the spike, then the spike dies
    frame();
    CHECK(started.size() == 1);
    level.world().destroy(spike);
    frame();
    CHECK(ended.size() == 1);
}

TEST_CASE("Collision: a steady-state update allocates nothing", "[scene][collision][allocation]") {
    Scene level("level");
    for (int i = 0; i < 100; ++i) {
        const ecs::Entity entity = level.spawn();
        level.add<Transform>(entity, Transform{.position = {static_cast<f32>(i) * 0.7F, 0.0F, 0.0F}});
        level.add<Collider>(entity);
    }
    for (int warmUp = 0; warmUp < 3; ++warmUp) {
        level.run(ecs::Phase::PreUpdate);
        level.run(ecs::Phase::PostUpdate);
    }
    u64 allocations = 0;
    {
        const test::AllocationCounter counter;
        level.run(ecs::Phase::PreUpdate);
        level.run(ecs::Phase::PostUpdate);
        allocations = counter.count();
    }
    CHECK(allocations == 0);
    CHECK(level.world().resource<CollisionState>().overlapping.size() == 99);
}

TEST_CASE("Collision: events name the other entity of a pair", "[scene][collision]") {
    ecs::World world;
    const ecs::Entity hero = world.create();
    const ecs::Entity coin = world.create();
    const ecs::Entity bystander = world.create();
    const CollisionStarted started{.a = hero, .b = coin};
    CHECK(started.other(hero) == coin);
    CHECK(started.other(coin) == hero);
    CHECK_FALSE(started.other(bystander).has_value());
    const CollisionEnded ended{.a = hero, .b = coin};
    CHECK(ended.other(coin) == hero);
    CHECK_FALSE(ended.other(bystander).has_value());
}
