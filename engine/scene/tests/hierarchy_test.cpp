// Transform hierarchies: setParent() keeps Parent and Children in step, the transform system propagates
// full 3D matrices top-down (also 64 levels deep and with children stored before their parents), destroying
// a parent destroys its subtree through commands, and a steady-state PostUpdate allocates nothing.

#include <teserra/scene/hierarchy.hpp>

#include <teserra/core/math.hpp>
#include <teserra/ecs/world.hpp>
#include <teserra/scene/scene.hpp>
#include <teserra/scene/transform.hpp>
#include <teserra/test/allocation_counter.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace teserra;
using namespace teserra::scene;
using Catch::Approx;

namespace {

void checkNear(const Vec3& actual, const Vec3& expected) {
    CHECK(actual.x == Approx(expected.x).margin(1e-4));
    CHECK(actual.y == Approx(expected.y).margin(1e-4));
    CHECK(actual.z == Approx(expected.z).margin(1e-4));
}

ecs::Entity spawnAt(Scene& scene, Vec3 position) {
    const ecs::Entity entity = scene.spawn();
    scene.add<Transform>(entity, Transform{.position = position});
    return entity;
}

} // namespace

TEST_CASE("Hierarchy: setParent and removeParent keep Parent and Children in step", "[scene][hierarchy]") {
    ecs::World world;
    const ecs::Entity a = world.create();
    const ecs::Entity b = world.create();
    const ecs::Entity child = world.create();
    const ecs::Entity other = world.create();

    setParent(world, child, a);
    setParent(world, other, a);
    CHECK(world.get<Parent>(child).entity == a);
    CHECK(world.get<Children>(a).entities == std::vector<ecs::Entity>{child, other});
    CHECK(isDescendantOf(world, child, a));
    CHECK_FALSE(isDescendantOf(world, a, child));

    setParent(world, child, a); // already its parent: nothing changes
    CHECK(world.get<Children>(a).entities.size() == 2);

    setParent(world, child, b); // reparenting moves it
    CHECK(world.get<Parent>(child).entity == b);
    CHECK(world.get<Children>(a).entities == std::vector<ecs::Entity>{other});
    CHECK(world.get<Children>(b).entities == std::vector<ecs::Entity>{child});

    removeParent(world, child);
    CHECK(world.tryGet<Parent>(child) == nullptr);
    CHECK(world.get<Children>(b).entities.empty());
    removeParent(world, child); // no parent: nothing to do
}

#if TESERRA_ENABLE_ASSERTS

TEST_CASE("Hierarchy: setParent refuses cycles", "[scene][hierarchy][validation]") {
    ecs::World world;
    const ecs::Entity root = world.create();
    const ecs::Entity middle = world.create();
    const ecs::Entity leaf = world.create();
    setParent(world, middle, root);
    setParent(world, leaf, middle);
    CHECK(test::requireAssert([&] { setParent(world, root, leaf); }).message().find("would make a cycle") !=
          std::string::npos);
    CHECK(test::requireAssert([&] { setParent(world, root, root); }).message().find("would make a cycle") !=
          std::string::npos);
}

#endif

TEST_CASE("Hierarchy: a child's world matrix is its parent's times its own, in 3D", "[scene][hierarchy]") {
    Scene level("level");
    const ecs::Entity body = spawnAt(level, {10.0F, 0.0F, 0.0F});
    Transform& bodyTransform = level.world().get<Transform>(body);
    bodyTransform.setRotation2D(math::radians(90.0F)); // local +X is world +Y
    bodyTransform.scale = Vec3{2.0F};
    const ecs::Entity hand = spawnAt(level, {1.0F, 0.0F, 0.0F});
    setParent(level.world(), hand, body);
    const ecs::Entity sword = spawnAt(level, {0.0F, 0.5F, 0.0F});
    setParent(level.world(), sword, hand);
    // A wheel tilted around +X: its child's local +Y points along world +Z.
    const ecs::Entity wheel = spawnAt(level, {0.0F, 0.0F, 0.0F});
    level.world().get<Transform>(wheel).rotation =
        math::angleAxis(math::radians(90.0F), Vec3{1.0F, 0.0F, 0.0F});
    const ecs::Entity spoke = spawnAt(level, {0.0F, 3.0F, 0.0F});
    setParent(level.world(), spoke, wheel);

    level.run(ecs::Phase::PostUpdate); // GlobalTransforms are added at the sync point, with their matrices

    checkNear(level.world().get<GlobalTransform>(hand).translation(), {10.0F, 2.0F, 0.0F});
    checkNear(level.world().get<GlobalTransform>(sword).translation(), {9.0F, 2.0F, 0.0F}); // hand's +Y is -X
    checkNear(level.world().get<GlobalTransform>(spoke).translation(), {0.0F, 0.0F, 3.0F});

    // Moving the parent moves the children on the next update.
    level.world().get<Transform>(body).position.y = 5.0F;
    level.run(ecs::Phase::PostUpdate);
    checkNear(level.world().get<GlobalTransform>(sword).translation(), {9.0F, 7.0F, 0.0F});
}

TEST_CASE("Hierarchy: 64 levels propagate, also with children stored before their parents",
          "[scene][hierarchy]") {
    Scene level("level");
    std::vector<ecs::Entity> chain;
    chain.reserve(64);
    for (int i = 0; i < 64; ++i) {
        chain.push_back(spawnAt(level, {1.0F, 0.0F, 0.0F}));
    }
    // Each entity's parent is the one created after it: the deepest child comes first in the pools.
    for (usize i = 0; i + 1 < chain.size(); ++i) {
        setParent(level.world(), chain[i], chain[i + 1]);
    }
    // A quarter turn at the root turns the whole chain.
    level.world().get<Transform>(chain.back()).setRotation2D(math::radians(90.0F));

    level.run(ecs::Phase::PostUpdate);
    checkNear(level.world().get<GlobalTransform>(chain.front()).translation(), {1.0F, 63.0F, 0.0F});
    level.run(ecs::Phase::PostUpdate); // and again, from the stored GlobalTransforms
    checkNear(level.world().get<GlobalTransform>(chain.front()).translation(), {1.0F, 63.0F, 0.0F});
    checkNear(level.world().get<GlobalTransform>(chain[32]).translation(), {1.0F, 31.0F, 0.0F});
}

TEST_CASE("Hierarchy: destroying a parent destroys its subtree through commands", "[scene][hierarchy]") {
    Scene level("level");
    const ecs::Entity root = spawnAt(level, {});
    const ecs::Entity child = spawnAt(level, {});
    const ecs::Entity grandchild = spawnAt(level, {});
    const ecs::Entity bystander = spawnAt(level, {});
    setParent(level.world(), child, root);
    setParent(level.world(), grandchild, child);
    level.run(ecs::Phase::PostUpdate);

    level.world().destroy(root);
    CHECK(level.world().isAlive(child)); // until the transform system runs
    level.run(ecs::Phase::PostUpdate);
    CHECK_FALSE(level.world().isAlive(child));
    CHECK_FALSE(level.world().isAlive(grandchild));
    CHECK(level.world().isAlive(bystander));
}

TEST_CASE("Hierarchy: a Parent set through commands is propagated and listed in Children",
          "[scene][hierarchy]") {
    Scene level("level");
    const ecs::Entity ship = spawnAt(level, {0.0F, 5.0F, 0.0F});
    level.systems()
        .add(ecs::Phase::Update, "attach",
             [ship](ecs::SystemContext& ctx) {
                 for (int i = 0; i < 2; ++i) {
                     const ecs::Entity turret = ctx.commands().create();
                     ctx.commands().add<Transform>(turret,
                                                   Transform{.position = {static_cast<f32>(i), 0.0F, 0.0F}});
                     ctx.commands().add<Parent>(turret, Parent{.entity = ship});
                 }
             })
        .writes<Transform>()
        .writes<Parent>();
    level.run(ecs::Phase::Update);
    level.run(ecs::Phase::PostUpdate);

    const Children* children = level.world().tryGet<Children>(ship);
    REQUIRE(children != nullptr);
    REQUIRE(children->entities.size() == 2);
    checkNear(level.world().get<GlobalTransform>(children->entities[1]).translation(), {1.0F, 5.0F, 0.0F});

    // Removing a Parent directly takes the entity out of the list at the next update.
    level.world().remove<Parent>(children->entities[0]);
    level.run(ecs::Phase::PostUpdate);
    CHECK(level.world().get<Children>(ship).entities.size() == 1);
}

TEST_CASE("Hierarchy: updateGlobalTransforms propagates on a plain World", "[scene][hierarchy]") {
    ecs::World world;
    const ecs::Entity parent = world.create();
    world.add<Transform>(parent, Transform{.position = {1.0F, 2.0F, 3.0F}});
    const ecs::Entity child = world.create();
    world.add<Transform>(child, Transform{.position = {1.0F, 1.0F, 1.0F}});
    setParent(world, child, parent);
    updateGlobalTransforms(world);
    checkNear(world.get<GlobalTransform>(child).translation(), {2.0F, 3.0F, 4.0F});
}

TEST_CASE("Hierarchy: a steady-state PostUpdate allocates nothing", "[scene][hierarchy][allocation]") {
    Scene level("level");
    for (int tree = 0; tree < 20; ++tree) {
        ecs::Entity parent = spawnAt(level, {static_cast<f32>(tree), 0.0F, 0.0F});
        for (int depth = 0; depth < 5; ++depth) {
            const ecs::Entity child = spawnAt(level, {0.0F, 1.0F, 0.0F});
            setParent(level.world(), child, parent);
            parent = child;
        }
    }
    level.run(ecs::Phase::PostUpdate);
    level.run(ecs::Phase::PostUpdate);

    u64 allocations = 0;
    {
        const test::AllocationCounter counter;
        level.run(ecs::Phase::PostUpdate);
        allocations = counter.count();
    }
    CHECK(allocations == 0);
}
