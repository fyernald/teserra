// Commands: deferred creates, destroys, adds and removes applied in recording order; reserved entities,
// discarded buffers, payloads that must not be memcpy-relocated, zero allocations in steady state, and a
// randomized comparison with a reference model.

#include <teserra/ecs/commands.hpp>

#include <teserra/test/allocation_counter.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace teserra;
using namespace teserra::ecs;

namespace {

struct Position {
    int x = 0;
};
struct Health {
    int points = 100;
};
struct Name {
    std::string value; // longer than the small-string buffer in the tests: not trivially relocatable
};
struct Frozen {}; // a tag

// Counts live instances, to check that discarded payloads are destroyed.
struct Counted {
    static inline int alive = 0;
    int value = 0; // a data component, not a tag
    Counted() { ++alive; }
    Counted(const Counted& /*other*/) { ++alive; }
    Counted(Counted&& /*other*/) noexcept { ++alive; }
    Counted& operator=(const Counted&) = default;
    Counted& operator=(Counted&&) noexcept = default;
    ~Counted() { --alive; }
};

// Remembers where it was constructed, to check payload alignment.
struct alignas(64) Aligned {
    static inline std::uintptr_t lastAddress = 0;
    std::array<int, 16> values{}; // exactly 64 bytes: no padding (MSVC C4324)
    explicit Aligned(int initial) {
        values[0] = initial;
        lastAddress =
            reinterpret_cast<std::uintptr_t>(this); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    }
};

// Larger than a payload chunk (16 KiB).
struct Big {
    std::array<int, 6000> values{};
};

} // namespace

TEST_CASE("Commands apply in recording order, entities created through them usable in the same buffer",
          "[ecs][commands]") {
    World world;
    const Entity existing = world.create();
    world.add<Position>(existing, 1);
    Commands commands(world);

    const Entity spawned = commands.create();
    commands.add<Position>(spawned, 5);
    commands.add<Health>(spawned, 30);
    commands.add<Frozen>(spawned);
    commands.remove<Health>(spawned); // after its add: gone at the end
    commands.remove<Position>(existing);
    commands.add<Position>(existing, 9); // after the remove: present with the new value
    const Entity shortLived = commands.create();
    commands.add<Health>(shortLived);
    commands.destroy(shortLived);
    CHECK(commands.size() == 10);

    commands.apply();
    CHECK(commands.empty());
    CHECK(world.entityCount() == 2);
    CHECK(world.get<Position>(spawned).x == 5);
    CHECK(world.has<Frozen>(spawned));
    CHECK_FALSE(world.has<Health>(spawned));
    CHECK(world.get<Position>(existing).x == 9);
    CHECK_FALSE(world.isAlive(shortLived));
}

TEST_CASE("Reserved entities are not alive until the commands are applied", "[ecs][commands]") {
    World world;
    const Entity alive = world.create();
    Commands commands(world);
    const Entity reserved = commands.create();

    CHECK(reserved != alive);
    CHECK_FALSE(world.isAlive(reserved));
    CHECK(world.entityCount() == 1);
    int visited = 0;
    world.forEachEntity([&](Entity /*entity*/) { ++visited; });
    CHECK(visited == 1);

    commands.apply();
    CHECK(world.isAlive(reserved));
    CHECK(world.entityCount() == 2);
}

TEST_CASE("A reserved entity is the one create() would have returned", "[ecs][commands]") {
    World direct;
    World deferred;
    for (World* world : {&direct, &deferred}) {
        const Entity first = world->create();
        static_cast<void>(world->create());
        world->destroy(first);
    }
    Commands commands(deferred);
    const Entity reserved = commands.create(); // reuses the freed index, with the next generation
    commands.apply();
    CHECK(reserved == direct.create());
}

TEST_CASE("Clearing releases reserved entities and destroys recorded components", "[ecs][commands]") {
    World world;
    const Entity target = world.create();
    Entity released;
    {
        Commands commands(world);
        released = commands.create();
        commands.add<Counted>(released);
        commands.add<Counted>(target);
        CHECK(Counted::alive == 2);
        // Destroyed without apply(): everything is discarded.
    }
    CHECK(Counted::alive == 0);
    CHECK_FALSE(world.isAlive(released));
    CHECK_FALSE(world.has<Counted>(target));
    CHECK(world.entityCount() == 1);

    const Entity next = world.create(); // the released index, with a new generation
    CHECK(next.index() == released.index());
    CHECK(next != released);
    CHECK_FALSE(world.isAlive(released));
}

TEST_CASE("Commands record during iteration and apply after it", "[ecs][commands]") {
    World world;
    for (int i = 0; i < 10; ++i) {
        world.add<Position>(world.create(), i);
    }
    Commands commands(world);
    bool spawned = false;
    world.forEachEntity([&](Entity entity) {
        commands.add<Frozen>(entity);
        if (!spawned) {
            commands.add<Position>(commands.create(), 1000); // reserving during forEachEntity()
            spawned = true;
        }
    });
    for (const auto& [entity, position] : world.view<const Position>()) {
        if (position.x % 2 == 0) {
            commands.destroy(entity);
        } else {
            commands.add<Position>(commands.create(), position.x * 10); // and while a view iterates
        }
    }
    CHECK(commands.size() == 10 + 2 + 5 + 5 * 2);
    CHECK(world.entityCount() == 10);

    commands.apply();
    CHECK(world.entityCount() == 5 + 5 + 1);
    int sum = 0;
    for (const auto& [entity, position] : world.view<const Position>()) {
        sum += position.x;
    }
    CHECK(sum == (1 + 3 + 5 + 7 + 9) * 11 + 1000);
    CHECK(world.view<Frozen>().leaderSize() == 5); // the 5 survivors of the 10 that were frozen
}

TEST_CASE("Payloads survive chunk growth and keep their alignment", "[ecs][commands]") {
    World world;
    Commands commands(world);
    std::vector<Entity> entities;
    entities.reserve(2000);
    for (int i = 0; i < 2000; ++i) { // 2000 strings are more than one 16 KiB chunk
        entities.push_back(world.create());
        commands.add<Name>(entities.back(), std::format("entity number {} with a name too long for SSO", i));
    }
    const Entity aligned = world.create();
    commands.add<Aligned>(aligned, 7);
    CHECK(Aligned::lastAddress % 64 == 0);
    const Entity big = world.create();
    Big value;
    value.values.back() = 42;
    commands.add<Big>(big, value);

    commands.apply();
    for (int i = 0; i < 2000; ++i) {
        REQUIRE(world.get<Name>(entities[static_cast<usize>(i)]).value ==
                std::format("entity number {} with a name too long for SSO", i));
    }
    CHECK(world.get<Aligned>(aligned).values[0] == 7);
    CHECK(world.get<Big>(big).values.back() == 42);
}

TEST_CASE("Steady-state recording and applying allocate nothing", "[ecs][commands][allocation]") {
    World world;
    Commands commands(world);
    std::vector<Entity> spawned;
    spawned.reserve(100);
    const auto frame = [&] {
        for (const Entity entity : spawned) {
            commands.destroy(entity);
        }
        spawned.clear();
        for (int i = 0; i < 100; ++i) {
            const Entity entity = commands.create();
            commands.add<Position>(entity, i);
            commands.add<Health>(entity);
            spawned.push_back(entity);
        }
        commands.apply();
    };
    frame(); // warm-up: pools, sparse pages, records and chunks grow once
    frame();

    u64 allocations = 0;
    {
        const test::AllocationCounter counter;
        frame();
        allocations = counter.count();
    }
    CHECK(allocations == 0);
    CHECK(world.entityCount() == 100);
}

#if TESERRA_ENABLE_ASSERTS

TEST_CASE("Commands for dead entities, and applying during iteration, assert",
          "[ecs][commands][validation]") {
    World world;
    const Entity dead = world.create();
    world.destroy(dead);
    Commands commands(world);

    CHECK(test::requireAssert([&] { commands.add<Position>(dead, 1); }).message() ==
          std::format("Commands::add({}) for entity {}, which is neither alive nor reserved",
                      componentName<Position>(), dead));
    CHECK(test::requireAssert([&] { commands.destroy(dead); }).message() ==
          std::format("Commands::destroy() for entity {}, which is neither alive nor reserved", dead));

    const Entity entity = world.create();
    world.add<Position>(entity);
    commands.remove<Position>(entity);
    CHECK(test::requireAssert([&] {
              for ([[maybe_unused]] const auto& element : world.view<Position>()) {
                  commands.apply(); // the view locks the Position pool
              }
          }).message() == std::format("Cannot remove {} while the pool is iterated; use Commands to defer it",
                                      componentName<Position>()));
    commands.clear();
    CHECK(world.has<Position>(entity));
}

#endif

TEST_CASE("Commands match a reference model after random batches", "[ecs][commands][model]") {
    for (const unsigned seed : {5U, 17U, 2027U}) {
        INFO("seed " << seed);
        std::mt19937 random(seed);
        const auto chance = [&](int percent) {
            return std::uniform_int_distribution<int>(0, 99)(random) < percent;
        };
        World world;
        Commands commands(world);

        // The state after every recorded command, which apply() must produce.
        struct Model {
            std::optional<int> health;
            std::optional<std::string> name;
            bool frozen = false;
        };
        std::map<Entity, Model> model;
        std::vector<Entity> order; // model keys in creation order, for picking at random

        const auto pick = [&]() -> std::optional<Entity> {
            if (order.empty()) {
                return std::nullopt;
            }
            return order[std::uniform_int_distribution<usize>(0, order.size() - 1)(random)];
        };

        for (int batch = 0; batch < 200; ++batch) {
            const int length = std::uniform_int_distribution<int>(1, 40)(random);
            for (int step = 0; step < length; ++step) {
                const std::optional<Entity> target = pick();
                const int operation = target ? std::uniform_int_distribution<int>(0, 5)(random) : 0;
                if (operation == 0) {
                    const Entity entity = commands.create();
                    model.emplace(entity, Model{});
                    order.push_back(entity);
                } else if (operation == 1) {
                    commands.destroy(*target);
                    model.erase(*target);
                    std::erase(order, *target);
                } else if (operation == 2) {
                    Model& state = model.at(*target);
                    if (state.health) {
                        commands.remove<Health>(*target);
                        state.health.reset();
                    } else {
                        const int points = std::uniform_int_distribution<int>(1, 1000)(random);
                        commands.add<Health>(*target, points);
                        state.health = points;
                    }
                } else if (operation == 3) {
                    Model& state = model.at(*target);
                    if (state.frozen) {
                        commands.remove<Frozen>(*target);
                    } else {
                        commands.add<Frozen>(*target);
                    }
                    state.frozen = !state.frozen;
                } else {
                    Model& state = model.at(*target);
                    if (state.name && chance(50)) {
                        commands.remove<Name>(*target);
                        state.name.reset();
                    } else if (!state.name) {
                        std::string value = std::format("name {} of batch {} padded beyond SSO", step, batch);
                        commands.add<Name>(*target, value);
                        state.name = std::move(value);
                    }
                }
            }
            if (chance(10)) {
                // Discard the batch: roll the model back to the world, which apply() would have changed.
                commands.clear();
                model.clear();
                order.clear();
                world.forEachEntity([&](Entity entity) {
                    Model state;
                    if (const Health* health = world.tryGet<Health>(entity)) {
                        state.health = health->points;
                    }
                    if (const Name* name = world.tryGet<Name>(entity)) {
                        state.name = name->value;
                    }
                    state.frozen = world.has<Frozen>(entity);
                    model.emplace(entity, state);
                    order.push_back(entity);
                });
                continue;
            }
            commands.apply();

            REQUIRE(world.entityCount() == model.size());
            for (const auto& [entity, state] : model) {
                REQUIRE(world.isAlive(entity));
                const Health* health = world.tryGet<Health>(entity);
                REQUIRE((health != nullptr) == state.health.has_value());
                if (health != nullptr) {
                    REQUIRE(health->points == *state.health);
                }
                const Name* name = world.tryGet<Name>(entity);
                REQUIRE((name != nullptr) == state.name.has_value());
                if (name != nullptr) {
                    REQUIRE(name->value == *state.name);
                }
                REQUIRE(world.has<Frozen>(entity) == state.frozen);
            }
        }
    }
}
