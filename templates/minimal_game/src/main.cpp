// A minimal game on Teserra: a hero sprite that moves with WASD or the arrow keys. Escape quits.
//
//   minimal_game                                 until Escape or close
//   minimal_game --frames 120 --hidden-window    a quick check that it starts and draws
//
// The hero is assets/hero.png, copied next to the executable by teserra_copy_assets() in CMakeLists.txt.

#include <teserra/teserra.hpp>

using namespace teserra;

// Components are plain data.
struct Player {
    f32 speed = 4.0F; // world units per second
};

TESERRA_COMPONENT(Player, "Player");

namespace {

// Systems are free functions that see the world through their context.
void movePlayer(ecs::SystemContext& ctx) {
    const auto& input = ctx.resource<const platform::Input>();
    Vec2 direction =
        input.axis2D(platform::Key::A, platform::Key::D, platform::Key::S, platform::Key::W) +
        input.axis2D(platform::Key::Left, platform::Key::Right, platform::Key::Down, platform::Key::Up);
    if (math::length(direction) > 1.0F) {
        direction = math::normalize(direction);
    }
    const f32 dt = ctx.resource<const scene::Time>().deltaSeconds();
    for (auto [entity, transform, player] : ctx.view<scene::Transform, const Player>()) {
        transform.setPosition2D(transform.position2D() + (direction * player.speed * dt));
    }
}

} // namespace

int main(int argc, char** argv) { // NOLINT(bugprone-exception-escape)
    engine::App app{{.title = "Minimal game", .windowSize = {1280, 720}, .args = {argc, argv}}};
    if (!app.isInitialized()) {
        return 1;
    }

    scene::Scene& level = app.scenes().create("level");

    // A missing file shows the magenta and black fallback texture and logs an error; the game keeps running.
    // The texture stays loaded while `heroTexture` lives.
    const auto heroTexture = app.assets().load<renderer::Texture>("assets://hero.png");

    // Camera: 10 world units tall, orthographic.
    const ecs::Entity camera = level.spawn();
    level.add<scene::Transform>(camera);
    level.add<scene::Camera>(camera,
                             scene::Camera{.projection = scene::Orthographic{.height = 10.0F},
                                           .clearColor = {.r = 0.1F, .g = 0.12F, .b = 0.16F, .a = 1.0F}});

    // The hero: one world unit square.
    const ecs::Entity hero = level.spawn();
    level.add<scene::Transform>(hero);
    level.add<scene::SpriteRenderer>(hero, scene::SpriteRenderer{.texture = heroTexture.get().handle()});
    level.add<Player>(hero);

    level.systems()
        .add(ecs::Phase::Update, "move_player", movePlayer)
        .reads<Player>()
        .writes<scene::Transform>()
        .readsResource<platform::Input>()
        .readsResource<scene::Time>();

    const int exitCode = app.run(level, {.update = [](engine::App& running, f32) {
                                     if (running.input().wasPressed(platform::Key::Escape)) {
                                         running.quit();
                                     }
                                 }});
    log::flush();
    return exitCode;
}
