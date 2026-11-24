// Scene sprites: a scene built through the public API (iteration 10, #76). A player sprite moves with WASD
// or the arrows over a field of grass tiles and coins; the camera follows it. Everything is an entity with
// components, and the gameplay code is systems in phases with declared access. The App extracts the scene's
// cameras and sprites and draws them; no render callback is needed.
//
//   teserra_scene_sprites                                  until Escape or close
//   teserra_scene_sprites --frames 120 --hidden-window     smoke run
//
// The art is one sprite sheet, assets/sprites.png next to the executable, loaded through the App's assets as
// assets://sprites.png and cut into a grid of 16x16 regions (iteration 11). Without the file, every sprite
// shows the magenta and black fallback and the log has one error.

#include <teserra/assets/asset_registry.hpp>
#include <teserra/core/log.hpp>
#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/ecs/schedule.hpp>
#include <teserra/ecs/system_context.hpp>
#include <teserra/engine/app.hpp>
#include <teserra/platform/input.hpp>
#include <teserra/platform/key.hpp>
#include <teserra/renderer/assets.hpp>
#include <teserra/renderer/sprite_sheet.hpp>
#include <teserra/scene/render_components.hpp>
#include <teserra/scene/scene.hpp>
#include <teserra/scene/time.hpp>
#include <teserra/scene/transform.hpp>

#include <cmath>
#include <optional>
#include <utility>

using namespace teserra;

// Components: plain data.
struct Player {
    f32 speed = 6.0F; // world units per second
};
struct Coin {
    f32 phase = 0.0F; // of its bobbing
};
struct CameraFollow {}; // a tag: this camera follows the player

TESERRA_COMPONENT(Player, "Player");
TESERRA_COMPONENT(Coin, "Coin");
TESERRA_COMPONENT(CameraFollow, "CameraFollow");

namespace {

constexpr int kFieldRadius = 12; // tiles from the center in each direction

// The cells of assets/sprites.png, 16x16 texels each, left to right.
constexpr u32 kGrass = 0;
constexpr u32 kHero = 1;
constexpr u32 kCoin = 2;
constexpr u32 kCellCount = 3;

// Systems: free functions that see the world through their SystemContext.

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
        constexpr auto kLimit = static_cast<f32>(kFieldRadius);
        Vec2 position = transform.position2D() + (direction * player.speed * dt);
        position.x = std::fmax(-kLimit, std::fmin(kLimit, position.x));
        position.y = std::fmax(-kLimit, std::fmin(kLimit, position.y));
        transform.setPosition2D(position);
    }
}

void bobCoins(ecs::SystemContext& ctx) {
    const auto elapsed = static_cast<f32>(ctx.resource<const scene::Time>().elapsedSeconds());
    for (auto [entity, transform, coin] : ctx.view<scene::Transform, const Coin>()) {
        transform.setRotation2D(std::sin((elapsed * 2.0F) + coin.phase) * 0.3F);
    }
}

// Runs after the player moved, before the transforms: the camera is where the player is this frame.
void followPlayer(ecs::SystemContext& ctx) {
    Vec2 target{0.0F};
    for (const auto& [entity, transform, player] : ctx.view<const scene::Transform, const Player>()) {
        target = transform.position2D();
    }
    for (auto [entity, transform, follow] : ctx.view<scene::Transform, const CameraFollow>()) {
        transform.setPosition2D(target);
    }
}

} // namespace

int main(int argc, char** argv) { // NOLINT(bugprone-exception-escape)
    engine::App app{{.title = "Teserra scene sprites", .windowSize = {1280, 720}, .args = {argc, argv}}};
    if (!app.isInitialized()) {
        return 1;
    }

    scene::Scene& level = app.scenes().create("level");
    // 16 texels per world unit: each cell is one unit square. The sheet keeps its texture loaded while it
    // lives.
    const std::optional<renderer::SpriteSheet> sheet = [&]() -> std::optional<renderer::SpriteSheet> {
        auto made = renderer::SpriteSheet::grid(app.assets().load<renderer::Texture>("assets://sprites.png"),
                                                {.cellSize = {16, 16}, .count = kCellCount});
        if (!made) {
            TESERRA_LOG_ERROR(LogCategory::App, "Cannot cut the sprite sheet: {}", made.error());
            return std::nullopt;
        }
        return std::move(made).value();
    }();
    if (!sheet) {
        return 1;
    }

    // Camera: 14 world units tall, orthographic, following the player.
    const ecs::Entity camera = level.spawn();
    level.add<scene::Transform>(camera);
    level.add<scene::Camera>(camera,
                             scene::Camera{.projection = scene::Orthographic{.height = 14.0F},
                                           .clearColor = {.r = 0.05F, .g = 0.07F, .b = 0.1F, .a = 1.0F}});
    level.add<CameraFollow>(camera);

    for (int y = -kFieldRadius; y <= kFieldRadius; ++y) {
        for (int x = -kFieldRadius; x <= kFieldRadius; ++x) {
            const ecs::Entity tile = level.spawn();
            level.add<scene::Transform>(
                tile, scene::Transform{.position = {static_cast<f32>(x), static_cast<f32>(y), 0.0F}});
            level.add<scene::SpriteRenderer>(tile, scene::spriteFromSheet(*sheet, kGrass));
        }
    }

    for (int i = 0; i < 12; ++i) {
        const f32 angle = static_cast<f32>(i) * 0.5236F; // 30 degrees apart on a circle
        const ecs::Entity coin = level.spawn();
        level.add<scene::Transform>(
            coin, scene::Transform{.position = {std::cos(angle) * 6.0F, std::sin(angle) * 6.0F, 0.0F}});
        scene::SpriteRenderer sprite = scene::spriteFromSheet(*sheet, kCoin);
        sprite.size = Vec2{0.7F};
        sprite.layer = 5;
        level.add<scene::SpriteRenderer>(coin, sprite);
        level.add<Coin>(coin, Coin{.phase = angle});
    }

    const ecs::Entity hero = level.spawn();
    level.add<scene::Transform>(hero);
    scene::SpriteRenderer heroSprite = scene::spriteFromSheet(*sheet, kHero);
    heroSprite.layer = 10;
    level.add<scene::SpriteRenderer>(hero, heroSprite);
    level.add<Player>(hero);

    level.systems()
        .add(ecs::Phase::Update, "move_player", movePlayer)
        .reads<Player>()
        .writes<scene::Transform>()
        .readsResource<platform::Input>()
        .readsResource<scene::Time>();
    level.systems()
        .add(ecs::Phase::Update, "bob_coins", bobCoins)
        .reads<Coin>()
        .writes<scene::Transform>()
        .readsResource<scene::Time>();
    level.systems()
        .add(ecs::Phase::PostUpdate, "follow_player", followPlayer)
        .reads<Player>()
        .reads<CameraFollow>()
        .writes<scene::Transform>()
        .before(scene::kTransformSystemName);

    const int exitCode = app.run(level, {.update = [](engine::App& running, f32) {
                                     if (running.input().wasPressed(platform::Key::Escape)) {
                                         running.quit();
                                     }
                                 }});
    log::flush();
    return exitCode;
}
