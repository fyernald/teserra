// World resources: typed per-world singletons, their checks, stable references, and destruction in reverse
// insertion order before the components.

#include <teserra/ecs/world.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <format>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace teserra;
using namespace teserra::ecs;

namespace {

struct Score {
    int points = 0;
};

// Has no move constructor: resources never move.
struct Locked {
    std::mutex mutex;
    int value = 3;
};

// The ids of destroyed Logged objects, in order. A fixed array: a destructor must not allocate (it could
// throw).
struct DestructionLog {
    std::array<int, 8> ids{};
    usize count = 0;
    void clear() noexcept { count = 0; }
    [[nodiscard]] std::vector<int> order() const {
        return {ids.begin(), ids.begin() + static_cast<std::ptrdiff_t>(count)};
    }
};
DestructionLog& destroyed() noexcept {
    static DestructionLog log;
    return log;
}

// Logs its destruction. Usable as a resource and as a component.
template <int Id>
struct Logged {
    bool owner = true;
    Logged() = default;
    Logged(const Logged&) = delete;
    Logged& operator=(const Logged&) = delete;
    Logged(Logged&& other) noexcept
        : owner(std::exchange(other.owner, false)) {}
    Logged& operator=(Logged&& other) noexcept {
        owner = std::exchange(other.owner, false);
        return *this;
    }
    ~Logged() {
        if (owner && destroyed().count < destroyed().ids.size()) {
            destroyed().ids.at(destroyed().count++) = Id;
        }
    }
};

template <int Id>
struct Filler {
    int value = Id;
};

template <int... Ids>
void insertFillers(World& world, std::integer_sequence<int, Ids...> /*ids*/) {
    (world.insertResource<Filler<Ids>>(), ...);
}

} // namespace

TEST_CASE("Resources are inserted, read, written and removed", "[ecs][resource]") {
    World world;
    CHECK_FALSE(world.hasResource<Score>());
    CHECK(world.tryResource<Score>() == nullptr);

    Score& score = world.insertResource<Score>(5);
    CHECK(score.points == 5);
    world.resource<Score>().points += 10;
    CHECK(world.resource<Score>().points == 15);
    const World& view = world;
    CHECK(view.resource<Score>().points == 15);
    CHECK(view.tryResource<Score>() == &score);
    CHECK(world.hasResource<Score>());
    CHECK(world.resourceCount() == 1);

    world.insertResource<Locked>(); // not movable
    CHECK(world.resource<Locked>().value == 3);

    world.removeResource<Score>();
    CHECK_FALSE(world.hasResource<Score>());
    CHECK(world.resourceCount() == 1);
}

TEST_CASE("References to resources stay valid while others are inserted", "[ecs][resource]") {
    World world;
    Score& score = world.insertResource<Score>(1);
    insertFillers(world, std::make_integer_sequence<int, 100>{});
    CHECK(world.resourceCount() == 101);
    CHECK(&world.resource<Score>() == &score);
    CHECK(world.resource<Filler<99>>().value == 99);
}

TEST_CASE("Resources are destroyed in reverse insertion order, before the components", "[ecs][resource]") {
    destroyed().clear();
    {
        World world;
        world.add<Logged<0>>(world.create()); // a component
        world.insertResource<Logged<1>>();
        world.insertResource<Logged<2>>();
        world.insertResource<Logged<3>>();
    }
    CHECK(destroyed().order() == std::vector<int>{3, 2, 1, 0});

    destroyed().clear();
    World target;
    target.insertResource<Logged<1>>();
    target.insertResource<Logged<2>>();
    World source;
    source.insertResource<Score>(7);
    target = std::move(source); // the old resources go, in reverse order
    CHECK(destroyed().order() == std::vector<int>{2, 1});
    CHECK(target.resource<Score>().points == 7);
    CHECK_FALSE(target.hasResource<Logged<1>>());
}

#if TESERRA_ENABLE_ASSERTS

TEST_CASE("Missing and duplicate resources assert with the type name", "[ecs][resource][validation]") {
    World world;
    const World& constWorld = world;
    const std::string missing = std::format("World has no resource {}", componentName<Score>());
    CHECK(test::requireAssert([&] { static_cast<void>(world.resource<Score>()); }).message() == missing);
    CHECK(test::requireAssert([&] { static_cast<void>(constWorld.resource<Score>()); }).message() == missing);
    CHECK(test::requireAssert([&] { world.removeResource<Score>(); }).message() == missing);

    world.insertResource<Score>();
    CHECK(test::requireAssert([&] { world.insertResource<Score>(1); }).message() ==
          std::format("World already has resource {}", componentName<Score>()));
    CHECK(world.resourceCount() == 1);
}

#endif
