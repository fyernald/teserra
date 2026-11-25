// ECS micro-benchmarks (#114, docs/architecture/ecs.md#roadmap-from-simple-and-correct-to-benchmarked-and-
// optimized): the baseline that ECS optimizations (per-entity component masks, sorted pools, owning groups)
// are measured against. Each benchmark builds its world outside the measured part, so only the operation
// named is timed. Run the Release build: `teserra_ecs_bench` (all), or one by name.

#include <teserra/core/types.hpp>
#include <teserra/ecs/type_key.hpp>
#include <teserra/ecs/world.hpp>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace teserra;
using namespace teserra::ecs;

namespace bench {

struct Position {
    f32 x = 0.0F;
    f32 y = 0.0F;
    f32 z = 0.0F;
};
struct Velocity {
    f32 x = 1.0F;
    f32 y = 0.5F;
    f32 z = 0.0F;
};
struct Health {
    i32 value = 100;
};
struct Selected {}; // a tag

} // namespace bench

TESERRA_COMPONENT(bench::Position, "Position");
TESERRA_COMPONENT(bench::Velocity, "Velocity");
TESERRA_COMPONENT(bench::Health, "Health");
TESERRA_COMPONENT(bench::Selected, "Selected");

namespace {

constexpr usize kEntities = 100'000;
constexpr usize kTagged = 10'000;

// `count` entities with a Position; every `velocityEvery`-th also with a Velocity and a Health (0: none).
std::vector<Entity> populate(World& world, usize count, usize velocityEvery) {
    std::vector<Entity> entities;
    entities.reserve(count);
    for (usize i = 0; i < count; ++i) {
        const Entity entity = world.create();
        world.add<bench::Position>(entity, bench::Position{.x = static_cast<f32>(i)});
        if (velocityEvery != 0 && i % velocityEvery == 0) {
            world.add<bench::Velocity>(entity);
            world.add<bench::Health>(entity);
        }
        entities.push_back(entity);
    }
    return entities;
}

} // namespace

// Creating entities: one record each, from the free list. Expected O(n).
TEST_CASE("ECS: create 100k entities", "[!benchmark][ecs]") {
    BENCHMARK_ADVANCED("create 100k")(Catch::Benchmark::Chronometer meter) {
        std::vector<World> worlds(static_cast<usize>(meter.runs()));
        meter.measure([&](int run) {
            World& world = worlds[static_cast<usize>(run)];
            for (usize i = 0; i < kEntities; ++i) {
                static_cast<void>(world.create());
            }
            return world.entityCount();
        });
    };
}

// Destroying entities that have a component: each destroy visits every pool (#145 targets this). O(n *
// pools).
TEST_CASE("ECS: destroy 100k entities", "[!benchmark][ecs]") {
    BENCHMARK_ADVANCED("destroy 100k")(Catch::Benchmark::Chronometer meter) {
        std::vector<World> worlds(static_cast<usize>(meter.runs()));
        std::vector<std::vector<Entity>> entities;
        entities.reserve(worlds.size());
        for (World& world : worlds) {
            entities.push_back(populate(world, kEntities, 2));
        }
        meter.measure([&](int run) {
            World& world = worlds[static_cast<usize>(run)];
            for (const Entity entity : entities[static_cast<usize>(run)]) {
                world.destroy(entity);
            }
            return world.entityCount();
        });
    };
}

// Iterating one component over 100k entities: a dense array walk. O(n).
TEST_CASE("ECS: iterate 1 component over 100k", "[!benchmark][ecs]") {
    World world;
    static_cast<void>(populate(world, kEntities, 0));
    BENCHMARK("iterate Position") {
        f32 sum = 0.0F;
        for (const auto [entity, position] : world.view<const bench::Position>()) {
            sum += position.x;
        }
        return sum;
    };
}

// Iterating three components that every entity has: the smallest pool drives, the others are looked up.
TEST_CASE("ECS: iterate 3 components over 100k", "[!benchmark][ecs]") {
    World world;
    static_cast<void>(populate(world, kEntities, 1));
    BENCHMARK("iterate Position, Velocity, Health") {
        for (auto [entity, position, velocity, health] :
             world.view<bench::Position, const bench::Velocity, const bench::Health>()) {
            position.x += velocity.x * static_cast<f32>(health.value);
        }
        return world.entityCount();
    };
}

// Adding a tag to 10k of 100k entities and removing it again, as a selection that changes every frame.
TEST_CASE("ECS: add and remove a tag on 10k of 100k entities", "[!benchmark][ecs]") {
    World world;
    const std::vector<Entity> entities = populate(world, kEntities, 0);
    BENCHMARK("add + remove Selected x 10k") {
        for (usize i = 0; i < kTagged; ++i) {
            world.add<bench::Selected>(entities[i * (kEntities / kTagged)]);
        }
        for (usize i = 0; i < kTagged; ++i) {
            world.remove<bench::Selected>(entities[i * (kEntities / kTagged)]);
        }
        return world.entityCount();
    };
}

// A view whose second component is on half of the entities: the smaller pool (50k) drives, half the lookups
// into the larger one hit.
TEST_CASE("ECS: view with 50% overlap over 100k", "[!benchmark][ecs]") {
    World world;
    static_cast<void>(populate(world, kEntities, 2));
    BENCHMARK("iterate Position with Velocity (50%)") {
        f32 sum = 0.0F;
        for (const auto [entity, position, velocity] :
             world.view<const bench::Position, const bench::Velocity>()) {
            sum += position.x * velocity.y;
        }
        return sum;
    };
}
