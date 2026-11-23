// The Schedule: systems in phases, ordered by registration and before()/after(), cycle and unknown-name
// errors, access declared and checked by SystemContext, and commands applied at the end of each phase.

#include <teserra/ecs/schedule.hpp>

#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace teserra;
using namespace teserra::ecs;

namespace test_types {
struct Position {
    f32 x = 0.0F;
};
struct Velocity {
    f32 x = 0.0F;
};
struct Frozen {};
struct Gravity {
    f32 value = -9.8F;
};
} // namespace test_types

TESERRA_COMPONENT(test_types::Position, "Position");
TESERRA_COMPONENT(test_types::Velocity, "Velocity");
TESERRA_COMPONENT(test_types::Frozen, "Frozen");
TESERRA_COMPONENT(test_types::Gravity, "Gravity");

using test_types::Frozen;
using test_types::Gravity;
using test_types::Position;
using test_types::Velocity;

namespace {

using Names = std::vector<std::string_view>;

// A system that only records that it ran.
Schedule::SystemFunction recorder(std::vector<std::string>& log, std::string name) {
    return [&log, name = std::move(name)](SystemContext&) { log.push_back(name); };
}

} // namespace

TEST_CASE("Schedule: phase names", "[ecs][schedule]") {
    CHECK(phaseName(Phase::Startup) == "Startup");
    CHECK(phaseName(Phase::FixedUpdate) == "FixedUpdate");
    CHECK(phaseName(Phase::Extract) == "Extract");
    CHECK(phaseName(Phase::Last) == "Last");
}

TEST_CASE("Schedule: systems without constraints run in registration order", "[ecs][schedule]") {
    Schedule schedule;
    World world;
    std::vector<std::string> log;
    schedule.add(Phase::Update, "c", recorder(log, "c"));
    schedule.add(Phase::Update, "a", recorder(log, "a"));
    schedule.add(Phase::PostUpdate, "late", recorder(log, "late"));
    schedule.add(Phase::Update, "b", recorder(log, "b"));

    CHECK(schedule.systemCount() == 4);
    CHECK(schedule.order(Phase::Update) == Names{"c", "a", "b"});
    CHECK(schedule.order(Phase::PostUpdate) == Names{"late"});
    CHECK(schedule.order(Phase::Startup).empty());

    schedule.run(Phase::Update, world);
    CHECK(log == std::vector<std::string>{"c", "a", "b"});
    schedule.run(Phase::PostUpdate, world);
    schedule.run(Phase::Last, world); // empty phase
    CHECK(log == std::vector<std::string>{"c", "a", "b", "late"});
}

TEST_CASE("Schedule: before() and after() reorder systems", "[ecs][schedule]") {
    Schedule schedule;
    World world;
    std::vector<std::string> log;
    schedule.add(Phase::Update, "render_prep", recorder(log, "render_prep")).after("movement");
    schedule.add(Phase::Update, "movement", recorder(log, "movement")).after("input");
    schedule.add(Phase::Update, "input", recorder(log, "input"));
    schedule.add(Phase::Update, "audio", recorder(log, "audio")).before("input");

    REQUIRE(schedule.build());
    // audio and input are ready first, audio before input; the chain follows.
    CHECK(schedule.order(Phase::Update) == Names{"audio", "input", "movement", "render_prep"});
    schedule.run(Phase::Update, world);
    CHECK(log == std::vector<std::string>{"audio", "input", "movement", "render_prep"});
}

TEST_CASE("Schedule: a long chain of constraints registered backwards", "[ecs][schedule]") {
    Schedule schedule;
    constexpr int kCount = 20;
    // s19 is registered first and must run last: s(i) runs after s(i-1).
    for (int i = kCount - 1; i >= 0; --i) {
        SystemConfig config =
            schedule.add(Phase::FixedUpdate, "s" + std::to_string(i), [](SystemContext&) {});
        if (i > 0) {
            config.after("s" + std::to_string(i - 1));
        }
    }
    REQUIRE(schedule.build());
    const Names order = schedule.order(Phase::FixedUpdate);
    REQUIRE(order.size() == kCount);
    for (int i = 0; i < kCount; ++i) {
        CHECK(order[static_cast<usize>(i)] == "s" + std::to_string(i));
    }
}

TEST_CASE("Schedule: cycles fail and name two systems on the cycle", "[ecs][schedule]") {
    SECTION("two systems") {
        Schedule schedule;
        schedule.add(Phase::Update, "a", [](SystemContext&) {}).before("b");
        schedule.add(Phase::Update, "b", [](SystemContext&) {}).before("a");
        const Result<void> built = schedule.build();
        REQUIRE_FALSE(built);
        CHECK(built.error().code == ErrorCode::InvalidArgument);
        CHECK(built.error().message == "Systems 'a' and 'b' form an ordering cycle in Update");
    }
    SECTION("three systems, with systems before and after the cycle") {
        Schedule schedule;
        // "after_cycle" is registered first, so the search for the cycle starts outside it.
        schedule.add(Phase::PostUpdate, "after_cycle", [](SystemContext&) {}).after("z");
        schedule.add(Phase::PostUpdate, "x", [](SystemContext&) {}).after("z").after("first");
        schedule.add(Phase::PostUpdate, "y", [](SystemContext&) {}).after("x");
        schedule.add(Phase::PostUpdate, "z", [](SystemContext&) {}).after("y");
        schedule.add(Phase::PostUpdate, "first", [](SystemContext&) {});
        const Result<void> built = schedule.build();
        REQUIRE_FALSE(built);
        const std::string& message = built.error().message;
        CHECK(message.ends_with("form an ordering cycle in PostUpdate"));
        CHECK(message.find("after_cycle") == std::string::npos);
        CHECK(message.find("first") == std::string::npos);
        // Two of x, y and z, as consecutive systems of the cycle (z before x before y before z).
        const bool namesCycleEdge = message.starts_with("Systems 'z' and 'x'") ||
                                    message.starts_with("Systems 'x' and 'y'") ||
                                    message.starts_with("Systems 'y' and 'z'");
        CHECK(namesCycleEdge);
    }
    SECTION("a system before itself") {
        Schedule schedule;
        schedule.add(Phase::Last, "loop", [](SystemContext&) {}).before("loop");
        const Result<void> built = schedule.build();
        REQUIRE_FALSE(built);
        CHECK(built.error().message == "Systems 'loop' and 'loop' form an ordering cycle in Last");
    }
}

TEST_CASE("Schedule: constraints on systems missing from the phase fail", "[ecs][schedule]") {
    Schedule schedule;
    schedule.add(Phase::PreUpdate, "input", [](SystemContext&) {});
    SECTION("unknown name") {
        schedule.add(Phase::Update, "movement", [](SystemContext&) {}).after("typo");
        const Result<void> built = schedule.build();
        REQUIRE_FALSE(built);
        CHECK(built.error().message ==
              "System 'movement' in Update runs after 'typo', which is not in that phase");
    }
    SECTION("a system of another phase") {
        schedule.add(Phase::Update, "movement", [](SystemContext&) {}).before("input");
        const Result<void> built = schedule.build();
        REQUIRE_FALSE(built);
        CHECK(built.error().message ==
              "System 'movement' in Update runs before 'input', which is not in that phase");
    }
}

TEST_CASE("Schedule: declared access lets systems use views and resources", "[ecs][schedule]") {
    Schedule schedule;
    World world;
    world.insertResource<Gravity>();
    const Entity moving = world.create();
    world.add<Position>(moving);
    world.add<Velocity>(moving, Velocity{2.0F});
    const Entity frozen = world.create();
    world.add<Position>(frozen);
    world.add<Velocity>(frozen, Velocity{5.0F});
    world.add<Frozen>(frozen);

    schedule
        .add(Phase::Update, "integrate",
             [](SystemContext& ctx) {
                 CHECK(ctx.systemName() == "integrate");
                 const f32 gravity = ctx.resource<const Gravity>().value;
                 for (auto [entity, position, velocity] :
                      ctx.view<Position, const Velocity>(exclude<Frozen>)) {
                     position.x += velocity.x + gravity;
                 }
                 CHECK(ctx.tryResource<const Gravity>() != nullptr);
             })
        .reads<Velocity>()
        .reads<Frozen>()
        .writes<Position>()
        .readsResource<Gravity>();
    schedule
        .add(Phase::Update, "weaken_gravity",
             [](SystemContext& ctx) {
                 ctx.resource<Gravity>().value = 0.0F;
                 // A write implies a read.
                 static_cast<void>(ctx.view<const Position>());
             })
        .writes<Position>()
        .writesResource<Gravity>();

    schedule.run(Phase::Update, world);
    CHECK(world.get<Position>(moving).x == 2.0F - 9.8F);
    CHECK(world.get<Position>(frozen).x == 0.0F);
    CHECK(world.resource<Gravity>().value == 0.0F);
}

TEST_CASE("Schedule: commands apply at the end of the phase", "[ecs][schedule]") {
    Schedule schedule;
    World world;
    usize seenBySpawner = 0;
    usize seenLaterInPhase = 0;
    usize seenNextPhase = 0;
    schedule
        .add(Phase::Update, "spawner",
             [&](SystemContext& ctx) {
                 for ([[maybe_unused]] const auto& element : ctx.view<const Position>()) {
                     ++seenBySpawner;
                 }
                 const Entity spawned = ctx.commands().create();
                 ctx.commands().add<Position>(spawned, Position{1.0F});
             })
        .reads<Position>();
    schedule
        .add(Phase::Update, "counter",
             [&](SystemContext& ctx) {
                 for ([[maybe_unused]] const auto& element : ctx.view<const Position>()) {
                     ++seenLaterInPhase;
                 }
             })
        .reads<Position>()
        .after("spawner");
    schedule
        .add(Phase::PostUpdate, "late_counter",
             [&](SystemContext& ctx) {
                 for ([[maybe_unused]] const auto& element : ctx.view<const Position>()) {
                     ++seenNextPhase;
                 }
             })
        .reads<Position>();

    schedule.run(Phase::Update, world);
    CHECK(seenBySpawner == 0);
    CHECK(seenLaterInPhase == 0); // not applied within the phase
    CHECK(world.entityCount() == 1);
    schedule.run(Phase::PostUpdate, world);
    CHECK(seenNextPhase == 1);
}

TEST_CASE("Schedule: FixedUpdate commands apply after each step", "[ecs][schedule]") {
    Schedule schedule;
    World world;
    std::vector<usize> counts;
    schedule.add(Phase::FixedUpdate, "step", [&](SystemContext& ctx) {
        counts.push_back(ctx.commands().world().entityCount());
        static_cast<void>(ctx.commands().create());
    });
    for (int step = 0; step < 3; ++step) {
        schedule.run(Phase::FixedUpdate, world);
    }
    CHECK(counts == std::vector<usize>{0, 1, 2});
    CHECK(world.entityCount() == 3);
}

TEST_CASE("Schedule: systems added after a run join the order", "[ecs][schedule]") {
    Schedule schedule;
    World world;
    std::vector<std::string> log;
    schedule.add(Phase::Update, "b", recorder(log, "b"));
    schedule.run(Phase::Update, world);
    schedule.add(Phase::Update, "a", recorder(log, "a")).before("b");
    schedule.run(Phase::Update, world);
    CHECK(log == std::vector<std::string>{"b", "a", "b"});
}

TEST_CASE("Schedule: running another world uses that world's commands", "[ecs][schedule]") {
    Schedule schedule;
    World first;
    World second;
    schedule.add(Phase::Update, "spawn",
                 [](SystemContext& ctx) { static_cast<void>(ctx.commands().create()); });
    schedule.run(Phase::Update, first);
    schedule.run(Phase::Update, second);
    schedule.run(Phase::Update, second);
    CHECK(first.entityCount() == 1);
    CHECK(second.entityCount() == 2);
}

#if TESERRA_ENABLE_ASSERTS

TEST_CASE("Schedule: undeclared access asserts with the system and type", "[ecs][schedule][validation]") {
    Schedule schedule;
    World world;
    world.insertResource<Gravity>();
    world.add<Position>(world.create());

    const auto failureOf = [&](std::string name, Schedule::SystemFunction function) {
        schedule.add(Phase::Update, std::move(name), std::move(function))
            .reads<Position>()
            .readsResource<Gravity>();
        return test::requireAssert([&] { schedule.run(Phase::Update, world); }).message();
    };

    SECTION("an undeclared view type") {
        CHECK(failureOf("move", [](SystemContext& ctx) { static_cast<void>(ctx.view<const Velocity>()); }) ==
              "System 'move' reads Velocity without declaring it (.reads<Velocity>())");
    }
    SECTION("a mutable view of a read-only type") {
        CHECK(failureOf("move", [](SystemContext& ctx) { static_cast<void>(ctx.view<Position>()); }) ==
              "System 'move' writes Position without declaring it (.writes<Position>())");
    }
    SECTION("an undeclared excluded type") {
        CHECK(failureOf("move", [](SystemContext& ctx) {
                  static_cast<void>(ctx.view<const Position>(exclude<Frozen>));
              }) == "System 'move' reads Frozen without declaring it (.reads<Frozen>())");
    }
    SECTION("an undeclared resource") {
        schedule.add(Phase::Update, "physics",
                     [](SystemContext& ctx) { static_cast<void>(ctx.resource<const Gravity>()); });
        CHECK(test::requireAssert([&] { schedule.run(Phase::Update, world); }).message() ==
              "System 'physics' reads resource Gravity without declaring it (.readsResource<Gravity>())");
    }
    SECTION("a written read-only resource") {
        CHECK(failureOf("physics", [](SystemContext& ctx) { ctx.resource<Gravity>().value = 0.0F; }) ==
              "System 'physics' writes resource Gravity without declaring it (.writesResource<Gravity>())");
    }
    SECTION("tryResource is checked too") {
        CHECK(
            failureOf("physics", [](SystemContext& ctx) { static_cast<void>(ctx.tryResource<Gravity>()); }) ==
            "System 'physics' writes resource Gravity without declaring it (.writesResource<Gravity>())");
    }
}

TEST_CASE("Schedule: misuse asserts", "[ecs][schedule][validation]") {
    Schedule schedule;
    World world;
    schedule.add(Phase::Update, "a", [](SystemContext&) {});

    SECTION("a duplicate name") {
        CHECK(test::requireAssert([&] {
                  schedule.add(Phase::PostUpdate, "a", [](SystemContext&) {});
              }).message() == "A system named 'a' already exists");
    }
    SECTION("adding from a running system") {
        schedule.add(Phase::Update, "adder",
                     [&](SystemContext&) { schedule.add(Phase::Update, "late", [](SystemContext&) {}); });
        CHECK(test::requireAssert([&] { schedule.run(Phase::Update, world); }).message() ==
              "Schedule::add() while the schedule runs");
        // The schedule recovers after the throwing run.
        CHECK(schedule.systemCount() == 2);
    }
    SECTION("running from a running system") {
        schedule.add(Phase::Update, "runner",
                     [&](SystemContext&) { schedule.run(Phase::PostUpdate, world); });
        CHECK(test::requireAssert([&] { schedule.run(Phase::Update, world); }).message() ==
              "Schedule::run() inside a system of the same schedule");
    }
    SECTION("a build error at run time") {
        schedule.add(Phase::Update, "b", [](SystemContext&) {}).after("missing");
        CHECK(test::requireAssert([&] { schedule.run(Phase::Update, world); }).message() ==
              "System 'b' in Update runs after 'missing', which is not in that phase");
    }
}

#endif
