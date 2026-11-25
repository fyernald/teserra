// Pixel perfect: a pixel-art scene rendered at 320x180 and scaled by whole factors to the window, letterboxed
// (iteration 13, #95). Resize the window: every art pixel stays a sharp square block. The hero moves with
// WASD or the arrows in whole art pixels; the camera is a child of the hero, and a minimap, rendered by a
// second camera into its own render target, is a child of the camera, so it stays in the corner of the
// screen.
//
//   F1   debug draw: the field's bounds and the hero's box
//   F2   switch between 320x180 and the window's own resolution
//   Esc  quit
//
//   teserra_pixel_perfect                                  until Escape or close
//   teserra_pixel_perfect --frames 120 --hidden-window     smoke run
//
// The art is drawn in code into one 64x16 texture: four 16x16 cells (grass, stone, hero, coin).

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
#include <teserra/renderer/debug_draw.hpp>
#include <teserra/renderer/render_target.hpp>
#include <teserra/renderer/sprite_sheet.hpp>
#include <teserra/rhi/sampler.hpp>
#include <teserra/scene/hierarchy.hpp>
#include <teserra/scene/render_components.hpp>
#include <teserra/scene/scene.hpp>
#include <teserra/scene/time.hpp>
#include <teserra/scene/transform.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

using namespace teserra;

struct Hero {
    f32 speed = 4.0F; // world units per second
    // Where the hero really is; its Transform shows this rounded to whole art pixels.
    Vec2 position{0.0F};
};

TESERRA_COMPONENT(Hero, "Hero");

namespace {

constexpr UVec2 kResolution{320, 180};
constexpr f32 kPixelsPerUnit = 16.0F;
// The camera shows the whole 320x180 target: one art texel per target pixel.
constexpr f32 kViewHeight = static_cast<f32>(kResolution.y) / kPixelsPerUnit;
constexpr f32 kViewWidth = static_cast<f32>(kResolution.x) / kPixelsPerUnit;
constexpr int kFieldRadius = 14;          // tiles from the center in each direction
constexpr UVec2 kMinimapSize{80, 45};     // pixels of the minimap target
constexpr f32 kMinimapViewHeight = 32.0F; // world units the minimap shows

constexpr u32 kGrass = 0;
constexpr u32 kStone = 1;
constexpr u32 kHeroCell = 2;
constexpr u32 kCoin = 3;
constexpr u32 kCellCount = 4;

using Texel = std::array<u8, 4>;

// The art: four 16x16 cells side by side, drawn texel by texel.
std::vector<std::byte> drawArt() {
    constexpr u32 kWidth = 16 * kCellCount;
    std::vector<std::byte> texels(usize{kWidth} * 16 * 4);
    const auto put = [&](u32 cell, u32 x, u32 y, Texel color) {
        const usize at = ((usize{y} * kWidth) + (cell * 16) + x) * 4;
        for (usize channel = 0; channel < 4; ++channel) {
            texels[at + channel] = std::byte{color[channel]};
        }
    };
    for (u32 y = 0; y < 16; ++y) {
        for (u32 x = 0; x < 16; ++x) {
            // Grass: two greens in a scattered pattern.
            const bool tuft = ((x * 7) + (y * 13)) % 11 == 0;
            put(kGrass, x, y, tuft ? Texel{88, 160, 64, 255} : Texel{62, 126, 52, 255});
            // Stone: bricks with dark mortar lines.
            const bool mortar = y % 8 == 0 || (x + ((y / 8) * 8)) % 16 == 0;
            put(kStone, x, y, mortar ? Texel{60, 60, 72, 255} : Texel{132, 132, 148, 255});
            // Hero: a round body with two eyes, transparent around it.
            const f32 dx = static_cast<f32>(x) - 7.5F;
            const f32 dy = static_cast<f32>(y) - 8.5F;
            const bool body = (dx * dx) + (dy * dy) < 36.0F;
            const bool eye = y == 7 && (x == 5 || x == 10);
            put(kHeroCell, x, y,
                eye    ? Texel{20, 20, 32, 255}
                : body ? Texel{232, 96, 72, 255}
                       : Texel{0, 0, 0, 0});
            // Coin: a small yellow ring.
            const f32 radius = (dx * dx) + ((dy + 1.0F) * (dy + 1.0F));
            const bool ring = radius < 16.0F;
            const bool shine = radius < 4.0F;
            put(kCoin, x, y,
                shine  ? Texel{255, 244, 160, 255}
                : ring ? Texel{240, 192, 48, 255}
                       : Texel{0, 0, 0, 0});
        }
    }
    return texels;
}

// Moves the hero, then shows its position in whole art pixels so that nothing shimmers.
void moveHero(ecs::SystemContext& ctx) {
    const auto& input = ctx.resource<const platform::Input>();
    Vec2 direction =
        input.axis2D(platform::Key::A, platform::Key::D, platform::Key::S, platform::Key::W) +
        input.axis2D(platform::Key::Left, platform::Key::Right, platform::Key::Down, platform::Key::Up);
    if (math::length(direction) > 1.0F) {
        direction = math::normalize(direction);
    }
    const f32 dt = ctx.resource<const scene::Time>().deltaSeconds();
    for (auto [entity, transform, hero] : ctx.view<scene::Transform, Hero>()) {
        constexpr auto kLimit = static_cast<f32>(kFieldRadius - 1);
        hero.position += direction * hero.speed * dt;
        hero.position.x = std::fmax(-kLimit, std::fmin(kLimit, hero.position.x));
        hero.position.y = std::fmax(-kLimit, std::fmin(kLimit, hero.position.y));
        transform.setPosition2D(
            Vec2{std::round(hero.position.x * kPixelsPerUnit), std::round(hero.position.y * kPixelsPerUnit)} /
            kPixelsPerUnit);
    }
}

void drawDebug(ecs::SystemContext& ctx) {
    auto& debug = ctx.resource<renderer::DebugDrawList>();
    constexpr auto kEdge = static_cast<f32>(kFieldRadius) + 0.5F;
    debug.rect({.position = Vec2{-kEdge}, .size = Vec2{2.0F * kEdge}},
               {.r = 1.0F, .g = 1.0F, .b = 0.0F, .a = 1.0F});
    for (const auto& [entity, global, hero] : ctx.view<const scene::GlobalTransform, const Hero>()) {
        const Vec3 center{global.matrix[3]};
        debug.rect({.position = Vec2{center} - Vec2{0.5F}, .size = Vec2{1.0F}},
                   {.r = 0.2F, .g = 1.0F, .b = 1.0F, .a = 1.0F});
    }
}

} // namespace

int main(int argc, char** argv) { // NOLINT(bugprone-exception-escape)
    engine::App app{{
        .title = "Teserra pixel perfect",
        .windowSize = {1280, 720},
        .sceneTarget = {.resolution = kResolution,
                        .letterboxColor = {.r = 0.02F, .g = 0.02F, .b = 0.03F, .a = 1.0F}},
        .spritePass = {.filter = rhi::Filter::Nearest},
        .args = {argc, argv},
    }};
    if (!app.isInitialized()) {
        return 1;
    }
    renderer::Renderer* const renderer = app.renderer();
    if (renderer == nullptr) {
        return 1; // logged
    }

    // The art, as a texture asset that the sprite sheet keeps loaded.
    const std::vector<std::byte> art = drawArt();
    rhi::Device& device = app.device();
    const assets::Asset<renderer::Texture> texture =
        app.assets().load<renderer::Texture>("game://art", [&]() -> Result<renderer::Texture> {
            TESERRA_TRY_ASSIGN(
                const rhi::TextureHandle handle,
                device.createTexture({.width = 16 * kCellCount, .height = 16, .debugName = "Art"}));
            device.writeTexture(handle, {.width = 16 * kCellCount, .height = 16}, art);
            return renderer::Texture(device, handle, {16 * kCellCount, 16});
        });
    auto sheet = renderer::SpriteSheet::grid(texture, {.cellSize = {16, 16}, .count = kCellCount});
    if (!sheet) {
        TESERRA_LOG_ERROR(LogCategory::App, "Cannot cut the art: {}", sheet.error());
        return 1;
    }

    // The minimap's target: a camera renders the whole field into it every frame.
    auto minimap = renderer->createRenderTarget({.size = kMinimapSize, .debugName = "Minimap"});
    if (!minimap) {
        TESERRA_LOG_ERROR(LogCategory::App, "Cannot create the minimap: {}", minimap.error());
        return 1;
    }

    scene::Scene& level = app.scenes().create("level");
    for (int y = -kFieldRadius; y <= kFieldRadius; ++y) {
        for (int x = -kFieldRadius; x <= kFieldRadius; ++x) {
            const bool wall = std::abs(x) == kFieldRadius || std::abs(y) == kFieldRadius ||
                              (x % 6 == 0 && y % 6 == 0 && (x != 0 || y != 0));
            const ecs::Entity tile = level.spawn();
            level.add<scene::Transform>(
                tile, scene::Transform{.position = {static_cast<f32>(x), static_cast<f32>(y), 0.0F}});
            level.add<scene::SpriteRenderer>(tile, scene::spriteFromSheet(*sheet, wall ? kStone : kGrass));
        }
    }
    for (int i = 0; i < 16; ++i) {
        const f32 angle = static_cast<f32>(i) * 0.3927F; // 22.5 degrees apart on a circle
        const ecs::Entity coin = level.spawn();
        // On whole art pixels, like everything else.
        level.add<scene::Transform>(
            coin, scene::Transform{.position = {std::round(std::cos(angle) * 160.0F) / 16.0F,
                                                std::round(std::sin(angle) * 160.0F) / 16.0F, 0.0F}});
        scene::SpriteRenderer sprite = scene::spriteFromSheet(*sheet, kCoin);
        sprite.layer = 5;
        level.add<scene::SpriteRenderer>(coin, sprite);
    }

    const ecs::Entity hero = level.spawn();
    level.add<scene::Transform>(hero);
    scene::SpriteRenderer heroSprite = scene::spriteFromSheet(*sheet, kHeroCell);
    heroSprite.layer = 10;
    level.add<scene::SpriteRenderer>(hero, heroSprite);
    level.add<Hero>(hero);

    // The camera is a child of the hero, so it follows it; it shows the 320x180 target one texel per pixel.
    const ecs::Entity camera = level.spawn();
    level.add<scene::Transform>(camera);
    level.add<scene::Camera>(camera,
                             scene::Camera{.projection = scene::Orthographic{.height = kViewHeight},
                                           .clearColor = {.r = 0.05F, .g = 0.07F, .b = 0.1F, .a = 1.0F}});
    scene::setParent(level.world(), camera, hero);

    // The minimap camera sees the whole field from the center and renders first, into the minimap target.
    const ecs::Entity mapCamera = level.spawn();
    level.add<scene::Transform>(mapCamera);
    level.add<scene::Camera>(mapCamera,
                             scene::Camera{.projection = scene::Orthographic{.height = kMinimapViewHeight},
                                           .order = -1,
                                           .clearColor = {.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F},
                                           .target = *minimap});
    // A sprite showing the minimap in the top-right corner of the view: a child of the camera. The minimap's
    // own camera leaves it out (a target is never sampled while it is drawn).
    const Vec2 mapSize =
        Vec2{static_cast<f32>(kMinimapSize.x), static_cast<f32>(kMinimapSize.y)} / kPixelsPerUnit;
    const ecs::Entity mapSprite = level.spawn();
    level.add<scene::Transform>(
        mapSprite, scene::Transform{.position = {(kViewWidth * 0.5F) - (mapSize.x * 0.5F) - 0.25F,
                                                 (kViewHeight * 0.5F) - (mapSize.y * 0.5F) - 0.25F, 0.0F}});
    level.add<scene::SpriteRenderer>(
        mapSprite, scene::SpriteRenderer{
                       .texture = renderer->renderTarget(*minimap)->texture, .size = mapSize, .layer = 100});
    scene::setParent(level.world(), mapSprite, camera);

    level.systems()
        .add(ecs::Phase::Update, "move_hero", moveHero)
        .writes<Hero>()
        .writes<scene::Transform>()
        .readsResource<platform::Input>()
        .readsResource<scene::Time>();
    level.systems()
        .add(ecs::Phase::PostUpdate, "draw_debug", drawDebug)
        .reads<Hero>()
        .reads<scene::GlobalTransform>()
        .writesResource<renderer::DebugDrawList>()
        .after(scene::kTransformSystemName);

    auto& debug = level.world().resource<renderer::DebugDrawList>();
    debug.setEnabled(false);
    const int exitCode =
        app.run(level, {.update = [&](engine::App& running, f32) {
                    const platform::Input& input = running.input();
                    if (input.wasPressed(platform::Key::Escape)) {
                        running.quit();
                    }
                    if (input.wasPressed(platform::Key::F1)) {
                        debug.setEnabled(!debug.isEnabled());
                    }
                    if (input.wasPressed(platform::Key::F2)) {
                        renderer::SceneTargetConfig target = renderer->sceneTarget();
                        target.resolution = target.resolution.x == 0 ? kResolution : UVec2{0U};
                        renderer->setSceneTarget(target);
                        TESERRA_LOG_INFO(LogCategory::App, "Scene resolution: {}",
                                         target.resolution.x == 0 ? "the window's" : "320x180");
                    }
                }});
    log::flush();
    return exitCode;
}
