// Scene sprites: a scene built through the public API (iteration 10, #76). A player sprite moves with WASD
// or the arrows over a field of grass tiles and coins; the camera follows it. Everything is an entity with
// components, and the gameplay code is systems in phases with declared access. The App extracts the scene's
// cameras and sprites and draws them; no render callback is needed.
//
//   teserra_scene_sprites                                  until Escape or close
//   teserra_scene_sprites --frames 120 --hidden-window     smoke run
//
// Textures are made in code and created on the App's device; asset handles come with iteration 11.

#include <teserra/core/log.hpp>
#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/ecs/schedule.hpp>
#include <teserra/ecs/system_context.hpp>
#include <teserra/engine/app.hpp>
#include <teserra/platform/input.hpp>
#include <teserra/platform/key.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/texture.hpp>
#include <teserra/scene/render_components.hpp>
#include <teserra/scene/scene.hpp>
#include <teserra/scene/time.hpp>
#include <teserra/scene/transform.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <string_view>
#include <vector>

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

constexpr u32 kTextureSize = 16;
constexpr int kFieldRadius = 12; // tiles from the center in each direction

// A kTextureSize² RGBA8 sRGB texture whose texel (x, y), top row first, is `texel(x, y)`.
rhi::TextureHandle makeTexture(rhi::Device& device, std::string_view name,
                               const std::function<std::array<u8, 4>(u32, u32)>& texel) {
    std::vector<std::byte> texels;
    texels.reserve(usize{kTextureSize} * kTextureSize * 4);
    for (u32 y = 0; y < kTextureSize; ++y) {
        for (u32 x = 0; x < kTextureSize; ++x) {
            for (const u8 channel : texel(x, y)) {
                texels.push_back(static_cast<std::byte>(channel));
            }
        }
    }
    const rhi::TextureHandle texture = *device.createTexture(
        {.width = kTextureSize, .height = kTextureSize, .debugName = std::string(name)});
    device.writeTexture(texture, {.width = kTextureSize, .height = kTextureSize}, texels);
    return texture;
}

// Distance of texel (x, y) from the texture's center, in texels.
f32 centerDistance(u32 x, u32 y) {
    constexpr f32 kCenter = (kTextureSize - 1) * 0.5F;
    return std::hypot(static_cast<f32>(x) - kCenter, static_cast<f32>(y) - kCenter);
}

struct Textures {
    rhi::TextureHandle grass;
    rhi::TextureHandle hero;
    rhi::TextureHandle coin;
};

Textures makeTextures(rhi::Device& device) {
    return {
        .grass = makeTexture(device, "Grass",
                             [](u32 x, u32 y) -> std::array<u8, 4> {
                                 const bool blade = ((x * 7) + (y * 13)) % 11 == 0;
                                 return blade ? std::array<u8, 4>{70, 150, 60, 255}
                                              : std::array<u8, 4>{50, 120, 45, 255};
                             }),
        // A round body with two eyes near the top, so the image's orientation is visible.
        .hero = makeTexture(device, "Hero",
                            [](u32 x, u32 y) -> std::array<u8, 4> {
                                if (centerDistance(x, y) > 7.5F) {
                                    return {0, 0, 0, 0};
                                }
                                const bool eye = y >= 4 && y <= 6 && (x == 5 || x == 10);
                                return eye ? std::array<u8, 4>{20, 20, 30, 255}
                                           : std::array<u8, 4>{230, 90, 60, 255};
                            }),
        .coin = makeTexture(device, "Coin",
                            [](u32 x, u32 y) -> std::array<u8, 4> {
                                const f32 distance = centerDistance(x, y);
                                if (distance > 6.0F) {
                                    return {0, 0, 0, 0};
                                }
                                return distance > 4.5F ? std::array<u8, 4>{200, 150, 30, 255}
                                                       : std::array<u8, 4>{250, 210, 60, 255};
                            }),
    };
}

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
    const Textures textures = makeTextures(app.device());

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
            level.add<scene::SpriteRenderer>(tile,
                                             scene::SpriteRenderer{.texture = textures.grass, .layer = 0});
        }
    }

    for (int i = 0; i < 12; ++i) {
        const f32 angle = static_cast<f32>(i) * 0.5236F; // 30 degrees apart on a circle
        const ecs::Entity coin = level.spawn();
        level.add<scene::Transform>(
            coin, scene::Transform{.position = {std::cos(angle) * 6.0F, std::sin(angle) * 6.0F, 0.0F}});
        level.add<scene::SpriteRenderer>(
            coin, scene::SpriteRenderer{.texture = textures.coin, .size = Vec2{0.7F}, .layer = 5});
        level.add<Coin>(coin, Coin{.phase = angle});
    }

    const ecs::Entity hero = level.spawn();
    level.add<scene::Transform>(hero);
    level.add<scene::SpriteRenderer>(hero, scene::SpriteRenderer{.texture = textures.hero, .layer = 10});
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
