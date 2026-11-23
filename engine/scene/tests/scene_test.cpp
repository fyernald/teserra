// Scenes and the SceneManager: names, switching at the frame boundary only, the Time resource, and the
// scene's world.

#include <teserra/scene/scene.hpp>

#include <teserra/scene/scene_manager.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace teserra;
using namespace teserra::scene;

namespace {

struct Position {
    int x = 0;
};
struct Player {}; // a tag

} // namespace

TEST_CASE("SceneManager: creates scenes and finds them by name", "[scene]") {
    SceneManager scenes;
    CHECK(scenes.active() == nullptr);
    Scene& menu = scenes.create("menu");
    Scene& level = scenes.create("level");

    CHECK(scenes.size() == 2);
    CHECK(menu.name() == "menu");
    CHECK(scenes.find("level") == &level);
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

#endif
