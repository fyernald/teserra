// A scene's systems: Startup runs once, phases run on the scene's world, and the engine's transform system
// gives new entities a correct GlobalTransform within the same PostUpdate.

#include <teserra/scene/scene.hpp>
#include <teserra/scene/transform.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace teserra;
using namespace teserra::scene;

TEST_CASE("Scene: Startup runs once, other phases every time", "[scene][schedule]") {
    Scene level("level");
    int startups = 0;
    int updates = 0;
    level.systems().add(ecs::Phase::Startup, "setup", [&](ecs::SystemContext&) { ++startups; });
    level.systems().add(ecs::Phase::Update, "tick", [&](ecs::SystemContext&) { ++updates; });

    CHECK_FALSE(level.started());
    level.run(ecs::Phase::Startup);
    CHECK(level.started());
    level.run(ecs::Phase::Startup);
    level.run(ecs::Phase::Update);
    level.run(ecs::Phase::Update);
    CHECK(startups == 1);
    CHECK(updates == 2);
}

TEST_CASE("Scene: every scene has the transform system first in PostUpdate", "[scene][schedule]") {
    Scene level("level");
    const std::vector<std::string_view> order = level.systems().order(ecs::Phase::PostUpdate);
    REQUIRE_FALSE(order.empty());
    CHECK(order.front() == kTransformSystemName); // then collisions, which need the world positions
    CHECK(kTransformSystemName == "teserra.transforms");
}

TEST_CASE("Scene: systems see the scene's world and Time", "[scene][schedule]") {
    Scene level("level");
    level.time().advance(0.5F, 0.25F);
    f32 seen = 0.0F;
    level.systems()
        .add(ecs::Phase::Update, "read_time",
             [&](ecs::SystemContext& ctx) { seen = ctx.resource<const Time>().deltaSeconds(); })
        .readsResource<Time>();
    level.run(ecs::Phase::Update);
    CHECK(seen == 0.5F);
}

TEST_CASE("Scene: a new entity has its GlobalTransform after the same PostUpdate", "[scene][schedule]") {
    Scene level("level");
    // Spawned through commands in Update, like gameplay code does.
    ecs::Entity spawned;
    level.systems().add(ecs::Phase::Update, "spawn", [&](ecs::SystemContext& ctx) {
        spawned = ctx.commands().create();
        ctx.commands().add<Transform>(spawned, Transform{.position = {3.0F, 4.0F, 5.0F}});
    });
    // A PostUpdate system ordered before the transforms moves the existing entity.
    const ecs::Entity existing = level.spawn();
    level.add<Transform>(existing);
    level.systems()
        .add(ecs::Phase::PostUpdate, "late_move",
             [&](ecs::SystemContext& ctx) {
                 for (auto [entity, transform] : ctx.view<Transform>()) {
                     transform.position.x += 1.0F;
                 }
             })
        .writes<Transform>()
        .before(kTransformSystemName);

    level.run(ecs::Phase::Update);
    REQUIRE(level.world().has<Transform>(spawned));
    CHECK_FALSE(level.world().has<GlobalTransform>(spawned));

    level.run(ecs::Phase::PostUpdate);
    REQUIRE(level.world().has<GlobalTransform>(spawned));
    REQUIRE(level.world().has<GlobalTransform>(existing));
    const Vec3 spawnedPosition = level.world().get<GlobalTransform>(spawned).translation();
    CHECK(spawnedPosition.x == Catch::Approx(4.0F)); // late_move ran before the transforms, on both
    CHECK(spawnedPosition.y == Catch::Approx(4.0F));
    CHECK(spawnedPosition.z == Catch::Approx(5.0F));
    CHECK(level.world().get<GlobalTransform>(existing).translation().x == Catch::Approx(1.0F));

    // Later frames keep the matrices current.
    level.world().get<Transform>(spawned).position.y = -2.0F;
    level.run(ecs::Phase::PostUpdate);
    CHECK(level.world().get<GlobalTransform>(spawned).translation().y == Catch::Approx(-2.0F));
}

namespace {
struct Pickup {
    int value = 0;
};
} // namespace

TEST_CASE("Scene: events sent in a frame are readable in it and the next, swapped at PreUpdate",
          "[scene][events]") {
    Scene level("level");
    ecs::Events<Pickup>& events = level.addEvents<Pickup>();
    CHECK(&level.addEvents<Pickup>() == &events);
    const auto frame = [&](std::optional<int> send) {
        level.run(ecs::Phase::PreUpdate);
        if (send) {
            events.send(Pickup{.value = *send});
        }
        return events.size();
    };
    CHECK(frame(1) == 1);            // frame 0
    CHECK(frame(std::nullopt) == 1); // frame 1: still readable
    CHECK(frame(std::nullopt) == 0); // frame 2: dropped
}
