// Components: stable TypeKeys, metadata, sparse-set pools (paged, swap-and-pop, tags without storage), the
// World's component API, iteration locks, and a randomized comparison against std::map / std::set models.

#include <teserra/ecs/world.hpp>
#include <teserra/test/allocation_counter.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <map>
#include <new>
#include <random>
#include <set>
#include <string_view>
#include <vector>

using namespace teserra;
using namespace teserra::ecs;

struct Velocity {
    float x = 0.0F;
};
TESERRA_COMPONENT(Velocity, "Velocity");

struct Position {
    int x = 0;
    int y = 0;
};
template <>
struct teserra::ecs::ComponentFields<Position> {
    static constexpr std::array<FieldDescriptor, 2> kFields{{
        {.name = "x", .offset = offsetof(Position, x), .type = FieldType::I32},
        {.name = "y", .offset = offsetof(Position, y), .type = FieldType::I32},
    }};
};

struct Frozen {}; // a tag

namespace game {
struct Plain {
    int value = 0;
};
} // namespace game

// Two types registered under one name, so that their TypeKeys collide.
struct CollideA {
    int value = 0;
};
struct CollideB {
    int value = 0;
};
TESERRA_COMPONENT(CollideA, "Same");
TESERRA_COMPONENT(CollideB, "Same");

namespace {

// Counts live instances, to check the type-erased lifecycle functions.
struct Tracked {
    static inline int live = 0;
    int value = 0;
    explicit Tracked(int v = 0)
        : value(v) {
        ++live;
    }
    Tracked(Tracked&& other) noexcept
        : value(other.value) {
        ++live;
    }
    Tracked& operator=(Tracked&&) noexcept = default;
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;
    ~Tracked() { --live; }
};

} // namespace

TEST_CASE("TypeKeys are stable hashes of the component names", "[ecs][components]") {
    // A golden value: it must never change, or saved data would stop matching its types.
    STATIC_CHECK(typeKey<Velocity>().value == 0xda19'653e'a580'e9b2ULL);
    STATIC_CHECK(componentName<Velocity>() == "Velocity");
    // Unregistered types get the compiler's name without "struct ", namespaces kept.
    STATIC_CHECK(componentName<Position>() == "Position");
    STATIC_CHECK(componentName<game::Plain>() == "game::Plain");
    STATIC_CHECK(typeKey<Position>() != typeKey<game::Plain>());
    STATIC_CHECK(typeKey<Position>() == typeKey("Position"));
}

TEST_CASE("Component metadata: size, alignment, tag, fields and lifecycle", "[ecs][components]") {
    const ComponentMeta& position = componentMeta<Position>();
    CHECK(position.key == typeKey<Position>());
    CHECK(position.name == "Position");
    CHECK(position.size == sizeof(Position));
    CHECK(position.alignment == alignof(Position));
    CHECK_FALSE(position.tag);
    REQUIRE(position.fields.size() == 2);
    CHECK(position.fields[1].name == "y");
    CHECK(position.fields[1].offset == offsetof(Position, y));

    const ComponentMeta& frozen = componentMeta<Frozen>();
    CHECK(frozen.tag);
    CHECK(frozen.size == 0);
    CHECK(frozen.fields.empty());

    // The erased functions move-construct and destroy real objects.
    alignas(Tracked) std::array<std::byte, sizeof(Tracked)> to{};
    Tracked from(7);
    const ComponentMeta& tracked = componentMeta<Tracked>();
    tracked.moveConstruct(to.data(), &from);
    auto* moved = std::launder(reinterpret_cast<Tracked*>(to.data()));
    CHECK(moved->value == 7);
    CHECK(Tracked::live == 2);
    tracked.destroy(moved);
    CHECK(Tracked::live == 1);
}

TEST_CASE("World: components are added, read, removed and destroyed with their entity", "[ecs][components]") {
    World world;
    const Entity a = world.create();
    const Entity b = world.create();

    world.add<Position>(a, 1, 2);
    world.add<Velocity>(a, 3.0F);
    world.add<Frozen>(b);
    CHECK(world.get<Position>(a).y == 2);
    CHECK(world.has<Velocity>(a));
    CHECK_FALSE(world.has<Velocity>(b));
    CHECK(world.tryGet<Velocity>(b) == nullptr);
    CHECK(world.has<Frozen>(b));
    CHECK(world.poolCount() == 3);

    world.get<Position>(a).x = 10;
    CHECK(world.pool<Position>().components()[0].x == 10);

    world.remove<Velocity>(a);
    CHECK_FALSE(world.has<Velocity>(a));
    world.destroy(a);
    CHECK_FALSE(world.has<Position>(a));
    CHECK(world.pool<Position>().size() == 0);
    CHECK(world.has<Frozen>(b));
}

TEST_CASE("A stale entity never matches the component of its index's new entity", "[ecs][components]") {
    World world;
    const Entity old = world.create();
    world.add<Position>(old);
    world.destroy(old);
    const Entity reused = world.create();
    world.add<Position>(reused, 5, 5);

    REQUIRE(reused.index() == old.index());
    CHECK(world.has<Position>(reused));
    CHECK_FALSE(world.has<Position>(old));
    CHECK(world.tryGet<Position>(old) == nullptr);
}

TEST_CASE("Removing swaps the last component into the hole", "[ecs][components]") {
    World world;
    std::vector<Entity> entities;
    entities.reserve(3);
    for (int i = 0; i < 3; ++i) {
        entities.push_back(world.create());
        world.add<Position>(entities.back(), i, 0);
    }
    world.remove<Position>(entities[0]);

    const ComponentPool<Position>& pool = world.pool<Position>();
    REQUIRE(pool.size() == 2);
    CHECK(pool.entities()[0] == entities[2]); // the last one moved into slot 0
    CHECK(pool.components()[0].x == 2);
    CHECK(world.get<Position>(entities[1]).x == 1);
    CHECK(world.get<Position>(entities[2]).x == 2);
}

TEST_CASE("Tags store no components, and sparse arrays allocate only the pages in use", "[ecs][components]") {
    STATIC_CHECK(ComponentPool<Frozen>::kTag);
    STATIC_CHECK_FALSE(ComponentPool<Position>::kTag);

    // The same entities with a tag allocate less than with a data component: only their entities.
    const auto allocationsFor = [](auto addOne) {
        World world;
        std::vector<Entity> entities;
        entities.reserve(1000);
        for (int i = 0; i < 1000; ++i) {
            entities.push_back(world.create());
        }
        const test::AllocationCounter counter;
        for (const Entity entity : entities) {
            addOne(world, entity);
        }
        return counter.count();
    };
    const u64 tags = allocationsFor([](World& world, Entity entity) { world.add<Frozen>(entity); });
    const u64 data = allocationsFor([](World& world, Entity entity) { world.add<Position>(entity); });
    CHECK(tags < data);

    World world;
    Entity far;
    for (int i = 0; i <= 100'000; ++i) {
        far = world.create();
    }
    world.add<Position>(far);
    CHECK(far.index() == 100'000);
    CHECK(world.pool<Position>().allocatedPages() == 1); // not the 25 pages up to that index
}

#if TESERRA_ENABLE_ASSERTS

TEST_CASE("Component misuse asserts", "[ecs][components][validation]") {
    World world;
    const Entity entity = world.create();
    world.add<Position>(entity);
    const Entity dead = world.create();
    world.destroy(dead);
    const auto failureOf = [](auto&& call) { return test::requireAssert(call).message(); };

    CHECK(failureOf([&] { world.add<Position>(entity); }) ==
          std::format("Entity {} already has Position", entity));
    CHECK(failureOf([&] { world.remove<Velocity>(entity); }) ==
          std::format("Removing Velocity from entity {}, which does not have it", entity));
    CHECK(failureOf([&] { static_cast<void>(world.get<Velocity>(entity)); }) ==
          std::format("Entity {} has no Velocity", entity));
    CHECK(failureOf([&] { world.add<Frozen>(dead); }) ==
          std::format("Adding Frozen to entity {}, which is not alive", dead));
}

TEST_CASE("A structural change on a locked pool asserts", "[ecs][components][validation]") {
    World world;
    const Entity first = world.create();
    const Entity second = world.create();
    world.add<Position>(first);
    {
        const ComponentPoolBase::Lock lock = world.pool<Position>().lock();
        CHECK(world.pool<Position>().isLocked());
        CHECK(test::requireAssert([&] { world.add<Position>(second); }).message() ==
              "Cannot add Position while the pool is iterated; use Commands to defer it");
        CHECK(test::requireAssert([&] { world.remove<Position>(first); }).message() ==
              "Cannot remove Position while the pool is iterated; use Commands to defer it");
        CHECK(test::requireAssert([&] { world.destroy(first); }).message() ==
              "Cannot remove Position while the pool is iterated; use Commands to defer it");
        world.add<Frozen>(second); // other pools are not locked
    }
    CHECK_FALSE(world.pool<Position>().isLocked());
    world.add<Position>(second);
}

TEST_CASE("Two component types with one TypeKey cannot share a world", "[ecs][components][validation]") {
    World world;
    const Entity entity = world.create();
    world.add<CollideA>(entity);
    CHECK(test::requireAssert([&] { static_cast<void>(world.has<CollideB>(entity)); }).message() ==
          "Component types Same and Same have the same TypeKey; register a different name for one");
}

#endif

TEST_CASE("World: random component operations match a reference model", "[ecs][components][model]") {
    for (const unsigned seed : {1U, 7U, 64U, 2026U}) {
        INFO("seed " << seed);
        std::mt19937 random(seed);
        const auto chance = [&](int percent) {
            return std::uniform_int_distribution<int>(0, 99)(random) < percent;
        };
        World world;
        std::vector<Entity> live;
        std::map<Entity, int> positions; // the reference: Position::x of the entities that have one
        std::set<Entity> frozen;

        const auto pick = [&] {
            return live[std::uniform_int_distribution<usize>(0, live.size() - 1)(random)];
        };
        for (int step = 0; step < 10'000; ++step) {
            if (live.empty() || chance(10)) {
                live.push_back(world.create());
                continue;
            }
            const Entity entity = pick();
            const int operation = std::uniform_int_distribution<int>(0, 5)(random);
            if (operation == 0) { // destroy
                world.destroy(entity);
                std::erase(live, entity);
                positions.erase(entity);
                frozen.erase(entity);
                REQUIRE_FALSE(world.has<Position>(entity));
                continue;
            }
            if (operation == 1 && !positions.contains(entity)) {
                world.add<Position>(entity, step, 0);
                positions[entity] = step;
            } else if (operation == 2 && positions.contains(entity)) {
                world.remove<Position>(entity);
                positions.erase(entity);
            } else if (operation == 3 && positions.contains(entity)) {
                world.get<Position>(entity).x += 1;
                positions[entity] += 1;
            } else if (operation == 4 && !frozen.contains(entity)) {
                world.add<Frozen>(entity);
                frozen.insert(entity);
            } else if (operation == 5 && frozen.contains(entity)) {
                world.remove<Frozen>(entity);
                frozen.erase(entity);
            }
            REQUIRE(world.has<Position>(entity) == positions.contains(entity));
            REQUIRE(world.has<Frozen>(entity) == frozen.contains(entity));
            if (positions.contains(entity)) {
                REQUIRE(world.get<Position>(entity).x == positions[entity]);
            }
        }

        // Every pool holds exactly the reference's entities, each with its value.
        const ComponentPool<Position>& pool = world.pool<Position>();
        REQUIRE(pool.size() == positions.size());
        for (usize i = 0; i < pool.size(); ++i) {
            REQUIRE(pool.components()[i].x == positions.at(pool.entities()[i]));
        }
        const std::span<const Entity> tagged = world.pool<Frozen>().entities();
        REQUIRE(std::set<Entity>(tagged.begin(), tagged.end()) == frozen);
    }
}

TEST_CASE("World::forEachComponent visits every component of an entity, read-only, tags included",
          "[ecs][components]") {
    World world;
    const Entity entity = world.create();
    const Entity other = world.create();
    world.add<Position>(entity, Position{.x = 3, .y = 4});
    world.add<Velocity>(entity, Velocity{.x = 1.5F});
    world.add<Frozen>(entity);
    world.add<game::Plain>(other);

    std::vector<std::string_view> names;
    const void* positionData = nullptr;
    const void* frozenData = &names;
    world.forEachComponent(entity, [&](const ComponentMeta& meta, const void* data) {
        names.push_back(meta.name);
        if (meta.name == "Position") {
            positionData = data;
            REQUIRE(meta.fields.size() == 2);
        }
        if (meta.name == "Frozen") {
            frozenData = data;
        }
    });

    std::ranges::sort(names);
    CHECK(names == std::vector<std::string_view>{"Frozen", "Position", "Velocity"});
    REQUIRE(positionData != nullptr);
    CHECK(positionData == world.tryGet<Position>(entity));
    const auto* position = static_cast<const Position*>(positionData);
    CHECK(position->y == 4);
    CHECK(frozenData == nullptr); // tags have no memory

    world.destroy(other);
    world.destroy(entity);
    const Entity reused = world.create(); // the same index, a new generation, no components
    int visited = 0;
    world.forEachComponent(reused, [&](const ComponentMeta&, const void*) { ++visited; });
    CHECK(visited == 0);
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("World::forEachComponent asserts on structural changes during the visit and on dead entities",
          "[ecs][components]") {
    World world;
    const Entity entity = world.create();
    world.add<Position>(entity);
    world.add<Velocity>(entity);

    REQUIRE_ASSERTS(
        world.forEachComponent(entity, [&](const ComponentMeta&, const void*) { (void)world.create(); }));
    REQUIRE_ASSERTS(
        world.forEachComponent(entity, [&](const ComponentMeta&, const void*) { world.destroy(entity); }));
    REQUIRE_ASSERTS(world.forEachComponent(entity, [&](const ComponentMeta& meta, const void*) {
        if (meta.name == "Velocity") {
            world.remove<Velocity>(entity);
        }
    }));
    REQUIRE_ASSERTS(world.forEachComponent(
        entity, [&](const ComponentMeta&, const void*) { world.add<game::Plain>(entity); }));

    const Entity dead = world.create();
    world.destroy(dead);
    REQUIRE_ASSERTS(world.forEachComponent(dead, [](const ComponentMeta&, const void*) {}));
}
#endif
