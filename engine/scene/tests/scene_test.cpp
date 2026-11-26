// Scenes and the SceneManager: names, switching at the frame boundary only, enter and exit hooks, reset,
// destroying scenes, the Time resource, and the scene's world.

#include <teserra/scene/scene.hpp>

#include <teserra/ecs/system_context.hpp>
#include <teserra/scene/collision.hpp>
#include <teserra/scene/scene_manager.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

using namespace teserra;
using namespace teserra::scene;

namespace {

struct Position {
    int x = 0;
};
struct Player {}; // a tag
struct Lives {    // a game resource
    int count = 3;
};
struct Hit {
    int amount = 0;
};

} // namespace

TEST_CASE("SceneManager: creates scenes and finds them by name", "[scene]") {
    SceneManager scenes;
    CHECK(scenes.active() == nullptr);
    Scene& menu = scenes.create("menu");
    Scene& level = scenes.create("level");

    CHECK(scenes.size() == 2);
    CHECK(menu.name() == "menu");
    CHECK(scenes.find("level") == &level);
    std::vector<std::string_view> names;
    scenes.forEach([&](Scene& scene) { names.push_back(scene.name()); });
    CHECK(names == std::vector<std::string_view>{"menu", "level"}); // in creation order
    CHECK(scenes.find("credits") == nullptr);
    CHECK(scenes.owns(menu));
    Scene elsewhere("elsewhere");
    CHECK_FALSE(scenes.owns(elsewhere));
    CHECK(scenes.active() == nullptr); // creating does not activate
}

TEST_CASE("SceneManager: a switch takes effect at the next frame boundary", "[scene]") {
    SceneManager scenes;
    Scene& menu = scenes.create("menu");
    Scene& level = scenes.create("level");
    Scene& credits = scenes.create("credits");

    scenes.switchTo(menu);
    CHECK(scenes.active() == nullptr);
    scenes.beginFrame();
    CHECK(scenes.active() == &menu);
    CHECK_FALSE(scenes.hasPendingSwitch());

    // Mid-frame requests: the frame keeps its scene, and the last request wins.
    scenes.switchTo(level);
    scenes.switchTo(credits);
    CHECK(scenes.hasPendingSwitch());
    CHECK(scenes.active() == &menu);
    scenes.beginFrame();
    CHECK(scenes.active() == &credits);
    scenes.beginFrame(); // nothing pending: unchanged
    CHECK(scenes.active() == &credits);
}

TEST_CASE("SceneManager: a switch runs the old scene's exit hook, then the new one's enter hook", "[scene]") {
    SceneManager scenes;
    Scene& menu = scenes.create("menu");
    Scene& level = scenes.create("level");
    std::vector<std::string> calls;
    for (Scene* scene : {&menu, &level}) {
        scene->onEnter([&](Scene& entered) {
            CHECK(scenes.active() == &entered); // already active while it builds itself
            calls.push_back(std::string(entered.name()) + " enter");
        });
        scene->onExit([&](Scene& left) { calls.push_back(std::string(left.name()) + " exit"); });
    }

    scenes.switchTo(menu);
    CHECK(calls.empty()); // nothing before the boundary
    scenes.beginFrame();
    CHECK(calls == std::vector<std::string>{"menu enter"}); // the first scene has nothing to exit

    calls.clear();
    scenes.switchTo(level);
    scenes.beginFrame();
    scenes.beginFrame(); // nothing pending: no hooks
    CHECK(calls == std::vector<std::string>{"menu exit", "level enter"});

    // Switching to the active scene restarts it.
    calls.clear();
    scenes.switchTo(level);
    scenes.beginFrame();
    CHECK(calls == std::vector<std::string>{"level exit", "level enter"});
    CHECK(scenes.active() == &level);

    // An empty function removes a hook.
    calls.clear();
    level.onExit({});
    scenes.switchTo(menu);
    scenes.beginFrame();
    CHECK(calls == std::vector<std::string>{"menu enter"});
}

TEST_CASE("SceneManager: a hook may request the next switch", "[scene]") {
    SceneManager scenes;
    Scene& splash = scenes.create("splash");
    Scene& menu = scenes.create("menu");
    splash.onEnter([&](Scene& /*entered*/) { scenes.switchTo(menu); });

    scenes.switchTo(splash);
    scenes.beginFrame();
    CHECK(scenes.active() == &splash); // the splash runs one frame
    CHECK(scenes.hasPendingSwitch());
    scenes.beginFrame();
    CHECK(scenes.active() == &menu);
}

TEST_CASE("Scene: reset destroys the entities and restarts the built-in state, keeping systems and "
          "resources",
          "[scene]") {
    Scene level("level");
    level.world().insertResource<Lives>(Lives{.count = 1});
    int startups = 0;
    int updates = 0;
    level.systems().add(ecs::Phase::Startup, "count_startups", [&](ecs::SystemContext&) { ++startups; });
    level.systems().add(ecs::Phase::Update, "count_updates", [&](ecs::SystemContext&) { ++updates; });
    level.addEvents<Hit>().send(Hit{.amount = 2});
    const ecs::Entity hero = level.spawn();
    level.add<Position>(hero, 5);
    level.add<Player>(hero);
    level.run(ecs::Phase::Startup);
    level.time().advance(0.5F, 0.01F);
    level.world().resource<CollisionState>().previous.emplace_back(hero, hero);

    level.reset();

    CHECK(level.world().entityCount() == 0);
    CHECK_FALSE(level.world().isAlive(hero));
    CHECK(level.time().frame() == 0);
    CHECK(level.time().elapsedSeconds() == 0.0);
    CHECK(level.world().resource<CollisionState>().previous.empty());
    CHECK(level.world().resource<ecs::Events<Hit>>().empty());
    CHECK(level.world().resource<Lives>().count == 1); // the game's resource stays
    CHECK_FALSE(level.started());
    // The systems stay, and Startup runs again.
    level.run(ecs::Phase::Startup);
    level.run(ecs::Phase::Update);
    CHECK(startups == 2);
    CHECK(updates == 1);
}

TEST_CASE("SceneManager: restarting a scene is one switch with a resetting exit hook", "[scene]") {
    SceneManager scenes;
    Scene& level = scenes.create("level");
    int builds = 0;
    level.onEnter([&](Scene& scene) {
        ++builds;
        for (int i = 0; i < 3; ++i) {
            scene.add<Position>(scene.spawn(), i);
        }
    });
    level.onExit([](Scene& scene) { scene.reset(); });
    scenes.switchTo(level);
    scenes.beginFrame();
    level.add<Position>(level.spawn(), 99); // gameplay added an entity
    CHECK(level.world().entityCount() == 4);

    scenes.switchTo(level);
    scenes.beginFrame();

    CHECK(builds == 2);
    CHECK(level.world().entityCount() == 3);
}

TEST_CASE("SceneManager: destroys a scene that is not running", "[scene]") {
    SceneManager scenes;
    Scene& menu = scenes.create("menu");
    Scene& level = scenes.create("level");
    bool exited = false;
    level.onExit([&](Scene& /*left*/) { exited = true; });
    scenes.switchTo(menu);
    scenes.beginFrame();

    scenes.destroy(level);

    CHECK(scenes.size() == 1);
    CHECK(scenes.find("level") == nullptr);
    CHECK(scenes.active() == &menu);
    CHECK_FALSE(exited);                       // it was not active
    static_cast<void>(scenes.create("level")); // the name is free again
}

TEST_CASE("Scene: owns a world with a Time resource, reached through spawn() and add()", "[scene]") {
    Scene level("level");
    CHECK(level.world().hasResource<Time>());
    CHECK(&level.time() == &level.world().resource<Time>());

    const ecs::Entity hero = level.spawn();
    level.add<Position>(hero, 3).x += 1;
    level.add<Player>(hero);
    CHECK(level.world().get<Position>(hero).x == 4);
    CHECK(level.world().has<Player>(hero));
}

TEST_CASE("Time: advances per frame and accumulates the scene's play time", "[scene]") {
    Time time;
    CHECK(time.frame() == 0);
    CHECK(time.elapsedSeconds() == 0.0);

    time.advance(0.016F, 0.01F);
    time.advance(0.034F, 0.01F);
    CHECK(time.frame() == 2);
    CHECK(time.deltaSeconds() == Catch::Approx(0.034F));
    CHECK(time.fixedStepSeconds() == Catch::Approx(0.01F));
    CHECK(time.elapsedSeconds() == Catch::Approx(0.05));
}

#if TESERRA_ENABLE_ASSERTS

TEST_CASE("SceneManager: duplicate names and foreign scenes assert", "[scene][validation]") {
    SceneManager scenes;
    static_cast<void>(scenes.create("level"));
    CHECK(test::requireAssert([&] { static_cast<void>(scenes.create("level")); }).message() ==
          "A scene named 'level' already exists");
    Scene elsewhere("elsewhere");
    CHECK(test::requireAssert([&] { scenes.switchTo(elsewhere); }).message() ==
          "Switching to scene 'elsewhere', which belongs to another SceneManager");
    CHECK_FALSE(scenes.hasPendingSwitch());
}

TEST_CASE("SceneManager: destroying a running or foreign scene asserts", "[scene][validation]") {
    SceneManager scenes;
    Scene& menu = scenes.create("menu");
    Scene& level = scenes.create("level");
    scenes.switchTo(menu);
    scenes.beginFrame();
    scenes.switchTo(level);

    CHECK(test::requireAssert([&] { scenes.destroy(menu); }).message() ==
          "Destroying scene 'menu', which is active");
    CHECK(test::requireAssert([&] { scenes.destroy(level); }).message() ==
          "Destroying scene 'level', which a switch is pending to");
    Scene elsewhere("elsewhere");
    CHECK(test::requireAssert([&] { scenes.destroy(elsewhere); }).message() ==
          "Destroying scene 'elsewhere', which belongs to another SceneManager");
    CHECK(scenes.size() == 2);
}

#endif
