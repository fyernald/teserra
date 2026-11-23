// Views: the entities with a set of components and without others, through structured bindings and each(),
// read-only const components, the smallest pool as leader, locks against structural changes, zero heap
// allocations while iterating, and a randomized comparison with sets computed from reference models.

#include <teserra/ecs/world.hpp>
#include <teserra/test/allocation_counter.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <random>
#include <set>
#include <tuple>
#include <type_traits>
#include <vector>

using namespace teserra;
using namespace teserra::ecs;

namespace {

struct Position {
    int x = 0;
};
struct Velocity {
    int dx = 0;
};
struct Health {
    int points = 100;
};
struct Frozen {}; // a tag

template <typename ViewType>
std::set<Entity> entitiesOf(ViewType&& view) {
    std::set<Entity> result;
    for (const auto& element : view) {
        result.insert(std::get<0>(element));
    }
    return result;
}

} // namespace

TEST_CASE("A view yields the entities with all included and no excluded components", "[ecs][view]") {
    World world;
    const Entity moving = world.create();
    world.add<Position>(moving, 1);
    world.add<Velocity>(moving, 2);
    const Entity still = world.create();
    world.add<Position>(still, 5);
    const Entity frozen = world.create();
    world.add<Position>(frozen, 7);
    world.add<Velocity>(frozen, 3);
    world.add<Frozen>(frozen);

    CHECK(entitiesOf(world.view<Position>()) == std::set<Entity>{moving, still, frozen});
    CHECK(entitiesOf(world.view<Position, const Velocity>()) == std::set<Entity>{moving, frozen});
    CHECK(entitiesOf(world.view<Position, const Velocity>(exclude<Frozen>)) == std::set<Entity>{moving});
    CHECK(entitiesOf(world.view<Frozen>()) == std::set<Entity>{frozen}); // a tag as the included type
    CHECK(world.view<Health>().empty());                                 // a type nobody has
}

TEST_CASE("Structured bindings and each() reach the components, const ones read-only", "[ecs][view]") {
    World world;
    for (int i = 0; i < 3; ++i) {
        const Entity entity = world.create();
        world.add<Position>(entity, 0);
        world.add<Velocity>(entity, i + 1);
    }

    for (auto [entity, position, velocity] : world.view<Position, const Velocity>()) {
        position.x += velocity.dx; // velocity is const Velocity&
    }
    int sum = 0;
    world.view<const Position>().each(
        [&](Entity /*entity*/, const Position& position) { sum += position.x; });
    CHECK(sum == 1 + 2 + 3);

    using Element = View<TypeList<Position, const Velocity>, TypeList<>>::Element;
    STATIC_CHECK(std::is_same_v<Element, std::tuple<Entity, Position&, const Velocity&>>);
    STATIC_CHECK_FALSE(std::is_assignable_v<std::tuple_element_t<2, Element>, Velocity>); // read-only
    STATIC_CHECK(std::is_assignable_v<std::tuple_element_t<1, Element>, Position>);
}

TEST_CASE("A view walks its smallest included pool", "[ecs][view]") {
    World world;
    for (int i = 0; i < 100; ++i) {
        const Entity entity = world.create();
        world.add<Position>(entity);
        if (i % 10 == 0) {
            world.add<Health>(entity);
        }
    }
    CHECK(world.view<Position, Health>().leaderSize() == 10);
    CHECK(world.view<Health, Position>().leaderSize() == 10);
    CHECK(entitiesOf(world.view<Position, Health>()).size() == 10);
}

TEST_CASE("Iterating a view allocates nothing", "[ecs][view][allocation]") {
    World world;
    for (int i = 0; i < 10'000; ++i) {
        const Entity entity = world.create();
        world.add<Position>(entity, i);
        world.add<Velocity>(entity, 1);
        if (i % 3 == 0) {
            world.add<Frozen>(entity);
        }
    }
    static_cast<void>(world.view<Position, const Velocity>(exclude<Frozen>)); // creates no pool later

    int visited = 0;
    u64 allocations = 0;
    {
        const test::AllocationCounter counter;
        for (auto [entity, position, velocity] : world.view<Position, const Velocity>(exclude<Frozen>)) {
            position.x += velocity.dx;
            ++visited;
        }
        world.view<Position>().each([](Entity /*entity*/, Position& position) { position.x -= 1; });
        allocations = counter.count();
    }
    CHECK(allocations == 0);
    CHECK(visited == 10'000 - 3334);
}

#if TESERRA_ENABLE_ASSERTS

TEST_CASE("Adding or removing a viewed component during iteration asserts", "[ecs][view][validation]") {
    World world;
    const Entity entity = world.create();
    world.add<Position>(entity);
    world.add<Velocity>(entity);
    const auto failureOf = [](auto&& call) { return test::requireAssert(call).message(); };

    CHECK(failureOf([&] {
              for (auto [e, position] : world.view<Position>()) {
                  world.remove<Position>(e);
              }
          }) == std::format("Cannot remove {} while the pool is iterated; use Commands to defer it",
                            componentName<Position>()));
    CHECK(failureOf([&] {
              for (auto [e, position] : world.view<Position>(exclude<Frozen>)) {
                  world.add<Frozen>(e); // excluded pools are locked too
              }
          }) == std::format("Cannot add {} while the pool is iterated; use Commands to defer it",
                            componentName<Frozen>()));
    // Other pools stay free, and the locks end with the view.
    for (const auto& [e, position] : world.view<Position>()) {
        world.add<Health>(e);
    }
    world.remove<Position>(entity);
}

#endif

TEST_CASE("Views match reference sets after random adds and removes", "[ecs][view][model]") {
    for (const unsigned seed : {3U, 11U, 2026U}) {
        INFO("seed " << seed);
        std::mt19937 random(seed);
        World world;
        std::vector<Entity> entities;
        entities.reserve(300);
        for (int i = 0; i < 300; ++i) {
            entities.push_back(world.create());
        }
        std::set<Entity> positions;
        std::set<Entity> velocities;
        std::set<Entity> healths;
        std::set<Entity> frozen;

        const auto toggle = [&](auto tag, std::set<Entity>& reference, Entity entity) {
            using T = decltype(tag);
            if (reference.contains(entity)) {
                world.remove<T>(entity);
                reference.erase(entity);
            } else {
                world.add<T>(entity);
                reference.insert(entity);
            }
        };
        const auto expected = [](const std::set<Entity>& a, const std::set<Entity>& b,
                                 const std::set<Entity>* c, const std::set<Entity>* without) {
            std::set<Entity> result;
            for (const Entity entity : a) {
                if (b.contains(entity) && (c == nullptr || c->contains(entity)) &&
                    (without == nullptr || !without->contains(entity))) {
                    result.insert(entity);
                }
            }
            return result;
        };

        for (int step = 0; step < 4'000; ++step) {
            const Entity entity =
                entities[std::uniform_int_distribution<usize>(0, entities.size() - 1)(random)];
            switch (std::uniform_int_distribution<int>(0, 3)(random)) {
                case 0:
                    toggle(Position{}, positions, entity);
                    break;
                case 1:
                    toggle(Velocity{}, velocities, entity);
                    break;
                case 2:
                    toggle(Health{}, healths, entity);
                    break;
                default:
                    toggle(Frozen{}, frozen, entity);
                    break;
            }
            if (step % 50 == 0) {
                REQUIRE(entitiesOf(world.view<Position, Velocity>()) ==
                        expected(positions, velocities, nullptr, nullptr));
                REQUIRE(entitiesOf(world.view<Position, const Velocity, Health>()) ==
                        expected(positions, velocities, &healths, nullptr));
                REQUIRE(entitiesOf(world.view<Position, Velocity>(exclude<Frozen>)) ==
                        expected(positions, velocities, nullptr, &frozen));
            }
        }
    }
}
