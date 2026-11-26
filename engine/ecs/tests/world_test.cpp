// The World's entity registry: generational entities that are never alive again once destroyed, FIFO reuse
// of indices, iteration over live entities with a lock against structural changes, independent worlds, and
// a randomized comparison against a naive reference model.

#include <teserra/ecs/world.hpp>

#include <teserra/ecs/commands.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <format>
#include <random>
#include <set>
#include <utility>
#include <vector>

using namespace teserra;
using namespace teserra::ecs;

namespace {

struct Mass {
    int value = 0;
};
struct Score {
    int points = 0;
};

std::vector<Entity> liveEntities(const World& world) {
    std::vector<Entity> entities;
    world.forEachEntity([&](Entity entity) { entities.push_back(entity); });
    return entities;
}

} // namespace

TEST_CASE("World: created entities are alive until destroyed", "[ecs][world]") {
    World world;
    CHECK(world.entityCount() == 0);
    CHECK_FALSE(world.isAlive(Entity{})); // the null entity is never alive

    const Entity first = world.create();
    const Entity second = world.create();
    CHECK_FALSE(first.isNull());
    CHECK(first != second);
    CHECK(world.isAlive(first));
    CHECK(world.isAlive(second));
    CHECK(world.entityCount() == 2);

    world.destroy(first);
    CHECK_FALSE(world.isAlive(first));
    CHECK(world.isAlive(second));
    CHECK(world.entityCount() == 1);
}

TEST_CASE("World: a destroyed entity stays stale when its index is reused", "[ecs][world]") {
    World world;
    const Entity old = world.create();
    world.destroy(old);

    const Entity reused = world.create(); // the only free index
    CHECK(reused.index() == old.index());
    CHECK(reused.generation() != old.generation());
    CHECK(world.isAlive(reused));
    CHECK_FALSE(world.isAlive(old));
}

TEST_CASE("World: freed indices are reused in the order they were freed", "[ecs][world]") {
    World world;
    std::vector<Entity> entities;
    entities.reserve(4);
    for (int i = 0; i < 4; ++i) {
        entities.push_back(world.create());
    }
    world.destroy(entities[2]);
    world.destroy(entities[0]);

    CHECK(world.create().index() == entities[2].index()); // freed first
    CHECK(world.create().index() == entities[0].index());
    CHECK(world.create().index() == 4); // then new indices
}

TEST_CASE("World: forEachEntity visits exactly the live entities", "[ecs][world]") {
    World world;
    const Entity a = world.create();
    const Entity b = world.create();
    const Entity c = world.create();
    world.destroy(b);

    CHECK(liveEntities(world) == std::vector<Entity>{a, c}); // index order
}

TEST_CASE("World: destroyAll destroys every live entity and keeps the resources", "[ecs][world]") {
    World world;
    world.insertResource<Score>(Score{.points = 7});
    std::vector<Entity> before;
    for (int i = 0; i < 5; ++i) {
        const Entity entity = world.create();
        world.add<Mass>(entity, Mass{.value = i});
        before.push_back(entity);
    }
    world.destroy(before[2]);

    world.destroyAll();

    CHECK(world.entityCount() == 0);
    CHECK(liveEntities(world).empty());
    for (const Entity entity : before) {
        CHECK_FALSE(world.isAlive(entity));
    }
    CHECK(world.resource<Score>().points == 7);
    // The components went with their entities: a new entity starts without them.
    const Entity fresh = world.create();
    CHECK_FALSE(world.has<Mass>(fresh));
    world.destroyAll(); // twice in a row, and on a world with one entity, is fine
    world.destroyAll();
    CHECK(world.entityCount() == 0);
}

TEST_CASE("World: destroyAll leaves entities that Commands reserved", "[ecs][world]") {
    World world;
    static_cast<void>(world.create());
    Commands commands(world);
    const Entity reserved = commands.create();
    commands.add<Mass>(reserved, Mass{.value = 3});

    world.destroyAll();
    CHECK(world.entityCount() == 0);
    commands.apply();

    CHECK(world.isAlive(reserved));
    CHECK(world.get<Mass>(reserved).value == 3);
    CHECK(world.entityCount() == 1);
}

TEST_CASE("World: several worlds are independent", "[ecs][world]") {
    World first;
    World second;
    const Entity inFirst = first.create();
    static_cast<void>(first.create());
    const Entity inSecond = second.create();

    CHECK(first.entityCount() == 2);
    CHECK(second.entityCount() == 1);
    second.destroy(inSecond);
    CHECK(first.isAlive(inFirst)); // same index, other world
    CHECK(second.entityCount() == 0);

    World moved = std::move(first);
    CHECK(moved.isAlive(inFirst));
    CHECK(moved.entityCount() == 2);
}

#if TESERRA_ENABLE_ASSERTS

TEST_CASE("World: destroying a stale or null entity asserts", "[ecs][world][validation]") {
    World world;
    const Entity entity = world.create();
    world.destroy(entity);

    CHECK(test::requireAssert([&] { world.destroy(entity); }).message() ==
          std::format("Destroying entity {}, which is not alive", entity));
    CHECK(test::requireAssert([&] { world.destroy(Entity{}); }).message() ==
          std::format("Destroying entity {}, which is not alive", Entity{}));
}

TEST_CASE("World: creating or destroying during forEachEntity asserts", "[ecs][world][validation]") {
    World world;
    const Entity entity = world.create();

    CHECK(test::requireAssert([&] {
              world.forEachEntity([&](Entity /*visited*/) { static_cast<void>(world.create()); });
          }).message() == "World::create() during forEachEntity(); use Commands to defer it");
    CHECK(test::requireAssert([&] {
              world.forEachEntity([&](Entity visited) { world.destroy(visited); });
          }).message() == "World::destroy() during forEachEntity(); use Commands to defer it");
    CHECK(test::requireAssert([&] {
              world.forEachEntity([&](Entity /*visited*/) { world.destroyAll(); });
          }).message() == "World::destroyAll() during forEachEntity(); use Commands to defer it");
    // The lock ends with the iteration, also after an assertion left it.
    world.destroy(entity);
    CHECK(world.entityCount() == 0);
}

#endif

TEST_CASE("World: random creates and destroys match a reference model", "[ecs][world][model]") {
    for (const unsigned seed : {1U, 2U, 3U, 42U, 2026U}) {
        INFO("seed " << seed);
        std::mt19937 random(seed);
        World world;
        std::set<Entity> reference;      // the live entities
        std::vector<Entity> everCreated; // live and stale

        for (int step = 0; step < 10'000; ++step) {
            const bool create = reference.empty() || std::uniform_int_distribution<int>(0, 99)(random) < 55;
            if (create) {
                const Entity entity = world.create();
                REQUIRE_FALSE(reference.contains(entity)); // never hands out a live entity twice
                reference.insert(entity);
                everCreated.push_back(entity);
            } else {
                auto victim = reference.begin();
                std::advance(victim, std::uniform_int_distribution<usize>(0, reference.size() - 1)(random));
                world.destroy(*victim);
                reference.erase(victim);
            }
            REQUIRE(world.entityCount() == reference.size());
        }
        // Every entity ever created is alive exactly when the reference says so, and iteration agrees.
        for (const Entity entity : everCreated) {
            REQUIRE(world.isAlive(entity) == reference.contains(entity));
        }
        const std::vector<Entity> live = liveEntities(world);
        REQUIRE(std::set<Entity>(live.begin(), live.end()) == reference);
    }
}
