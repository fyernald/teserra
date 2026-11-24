// Tilemap: a 256x256 map with three layers (ground, walls, decor) drawn from static chunk meshes (iteration
// 12, #89). The camera scrolls over the map on its own and zooms in and out; WASD or the arrows take over, Q
// and E zoom, and Space plants flowers under the camera, which rebuilds only that chunk. The title shows how
// many chunks were drawn, culled and rebuilt.
//
//   teserra_tilemap                                  until Escape or close
//   teserra_tilemap --frames 120 --hidden-window     smoke run
//
// The tiles are assets/tiles.png next to the executable, loaded as assets://tiles.png and cut into a grid of
// 16x16 tiles.

#include <teserra/core/log.hpp>
#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/ecs/schedule.hpp>
#include <teserra/ecs/system_context.hpp>
#include <teserra/engine/app.hpp>
#include <teserra/platform/input.hpp>
#include <teserra/platform/key.hpp>
#include <teserra/platform/window.hpp>
#include <teserra/renderer/assets.hpp>
#include <teserra/scene/render_components.hpp>
#include <teserra/scene/render_extraction.hpp>
#include <teserra/scene/scene.hpp>
#include <teserra/scene/tile_queries.hpp>
#include <teserra/scene/tilemap.hpp>
#include <teserra/scene/tileset.hpp>
#include <teserra/scene/time.hpp>
#include <teserra/scene/transform.hpp>

#include <cmath>
#include <format>
#include <optional>
#include <utility>
#include <variant>

using namespace teserra;

// The camera: scrolls on its own until a key moves it.
struct Scroller {
    bool manual = false;
    f32 zoom = 1.0F;
};
TESERRA_COMPONENT(Scroller, "Scroller");

namespace {

constexpr i32 kMapSize = 256;      // tiles per side, centered on the origin
constexpr f32 kBaseHeight = 24.0F; // world units the camera shows vertically at zoom 1

// The tiles of assets/tiles.png, as tile ids (cell index + 1).
constexpr scene::TileId kGrass = 1;
constexpr scene::TileId kDirt = 2;
constexpr scene::TileId kWater = 3;
constexpr scene::TileId kWall = 4;
constexpr scene::TileId kFlowers = 5;
constexpr scene::TileId kBush = 6;
constexpr scene::TileId kRock = 7;

// A cheap deterministic hash of a tile coordinate, in [0, 1).
f32 noise(i32 x, i32 y, u32 seed) {
    u32 h = (static_cast<u32>(x) * 73856093U) ^ (static_cast<u32>(y) * 19349663U) ^ (seed * 83492791U);
    h ^= h >> 13U;
    h *= 0x5bd1e995U;
    h ^= h >> 15U;
    return static_cast<f32>(h & 0xFFFFU) / 65536.0F;
}

scene::Tilemap buildMap() {
    scene::Tilemap map{.tileSize = {1.0F, 1.0F}};
    // Add every layer before holding references: addLayer() may move the layers added before.
    map.addLayer("ground");
    map.addLayer("walls");
    map.addLayer("decor");
    scene::TilemapLayer& ground = map.layers[0];
    scene::TilemapLayer& walls = map.layers[1];
    scene::TilemapLayer& decor = map.layers[2];
    constexpr i32 kHalf = kMapSize / 2;
    for (i32 y = -kHalf; y < kHalf; ++y) {
        for (i32 x = -kHalf; x < kHalf; ++x) {
            const auto fx = static_cast<f32>(x);
            const auto fy = static_cast<f32>(y);
            // Lakes where two waves overlap, dirt paths along a third.
            const f32 wave = std::sin(fx * 0.07F) + std::cos(fy * 0.09F) + std::sin((fx + fy) * 0.03F);
            const bool water = wave > 1.9F;
            const bool path = std::abs(std::sin((fx * 0.05F) + (fy * 0.11F))) < 0.08F;
            ground.setTile({x, y}, water ? kWater : (path ? kDirt : kGrass));
            if (water || path) {
                continue;
            }
            // Walled yards on a 24-tile grid, with a gap in each wall.
            const i32 gx = ((x % 24) + 24) % 24;
            const i32 gy = ((y % 24) + 24) % 24;
            const bool yard = noise(x / 24, y / 24, 1) < 0.35F;
            if (yard && (gx == 2 || gx == 14 || gy == 2 || gy == 14) && gx >= 2 && gx <= 14 && gy >= 2 &&
                gy <= 14 && gx != 8 && gy != 8) {
                walls.setTile({x, y}, kWall);
                continue;
            }
            const f32 scatter = noise(x, y, 2);
            if (scatter < 0.04F) {
                decor.setTile({x, y}, kFlowers);
            } else if (scatter < 0.06F) {
                decor.setTile({x, y}, kBush);
            } else if (scatter < 0.07F) {
                decor.setTile({x, y}, kRock);
            }
        }
    }
    // A frame of walls around the map.
    for (i32 i = -kHalf; i < kHalf; ++i) {
        walls.setTile({i, -kHalf}, kWall);
        walls.setTile({i, kHalf - 1}, kWall);
        walls.setTile({-kHalf, i}, kWall);
        walls.setTile({kHalf - 1, i}, kWall);
    }
    return map;
}

void moveCamera(ecs::SystemContext& ctx) {
    const auto& input = ctx.resource<const platform::Input>();
    const auto& time = ctx.resource<const scene::Time>();
    const f32 dt = time.deltaSeconds();
    const auto elapsed = static_cast<f32>(time.elapsedSeconds());
    const Vec2 direction =
        input.axis2D(platform::Key::A, platform::Key::D, platform::Key::S, platform::Key::W) +
        input.axis2D(platform::Key::Left, platform::Key::Right, platform::Key::Down, platform::Key::Up);
    const f32 zoomInput =
        (input.isDown(platform::Key::E) ? 1.0F : 0.0F) - (input.isDown(platform::Key::Q) ? 1.0F : 0.0F);

    for (auto [entity, transform, camera, scroller] : ctx.view<scene::Transform, scene::Camera, Scroller>()) {
        scroller.manual = scroller.manual || math::length(direction) > 0.0F || zoomInput != 0.0F;
        constexpr f32 kLimit = static_cast<f32>(kMapSize) / 2.0F;
        Vec2 position = transform.position2D();
        if (scroller.manual) {
            position += direction * (20.0F / scroller.zoom) * dt;
            scroller.zoom = std::fmax(0.1F, std::fmin(8.0F, scroller.zoom * std::exp(zoomInput * dt)));
        } else {
            // A slow figure eight over most of the map, zooming out and back in.
            position = {std::sin(elapsed * 0.11F) * 100.0F, std::sin(elapsed * 0.22F) * 60.0F};
            scroller.zoom = 1.0F + (0.8F * std::sin(elapsed * 0.3F));
        }
        position.x = std::fmax(-kLimit, std::fmin(kLimit, position.x));
        position.y = std::fmax(-kLimit, std::fmin(kLimit, position.y));
        transform.setPosition2D(position);
        if (auto* ortho = std::get_if<scene::Orthographic>(&camera.projection)) {
            ortho->height = kBaseHeight / scroller.zoom;
        }
    }
}

// Space plants flowers in a small cross under the camera: only the chunks they fall in rebuild.
void plantFlowers(ecs::SystemContext& ctx) {
    if (!ctx.resource<const platform::Input>().wasPressed(platform::Key::Space)) {
        return;
    }
    Vec3 target{0.0F};
    for (const auto& [entity, transform, scroller] : ctx.view<const scene::Transform, const Scroller>()) {
        target = transform.position;
    }
    for (auto [entity, map, global] : ctx.view<scene::Tilemap, const scene::GlobalTransform>()) {
        const scene::TilemapQuery query(map, global.matrix);
        const IVec2 center = query.worldToTile(target);
        scene::TilemapLayer* decor = map.findLayer("decor");
        if (decor == nullptr) {
            continue;
        }
        for (const IVec2 offset : {IVec2{0, 0}, IVec2{1, 0}, IVec2{-1, 0}, IVec2{0, 1}, IVec2{0, -1}}) {
            decor->setTile(center + offset, kFlowers);
        }
    }
}

} // namespace

int main(int argc, char** argv) { // NOLINT(bugprone-exception-escape)
    engine::App app{{.title = "Teserra tilemap", .windowSize = {1280, 720}, .args = {argc, argv}}};
    if (!app.isInitialized()) {
        return 1;
    }

    // Tiles 4 and up are drawn over the ground, and walls are solid (for tile queries).
    const std::optional<scene::Tileset> tileset = [&]() -> std::optional<scene::Tileset> {
        auto made = scene::Tileset::grid(app.assets().load<renderer::Texture>("assets://tiles.png"),
                                         {.cellSize = {16, 16}, .count = 7}, {{}, {}, {}, {.solid = true}});
        if (!made) {
            TESERRA_LOG_ERROR(LogCategory::App, "Cannot cut the tileset: {}", made.error());
            return std::nullopt;
        }
        return std::move(made).value();
    }();
    if (!tileset) {
        return 1;
    }

    scene::Scene& level = app.scenes().create("level");
    const ecs::Entity camera = level.spawn();
    level.add<scene::Transform>(camera);
    level.add<scene::Camera>(camera,
                             scene::Camera{.projection = scene::Orthographic{.height = kBaseHeight},
                                           .clearColor = {.r = 0.02F, .g = 0.02F, .b = 0.04F, .a = 1.0F}});
    level.add<Scroller>(camera);

    const ecs::Entity map = level.spawn();
    level.add<scene::Transform>(map);
    level.add<scene::Tilemap>(map, buildMap());
    level.add<scene::TilemapRenderer>(map, scene::TilemapRenderer{.tileset = &*tileset});

    level.systems()
        .add(ecs::Phase::Update, "move_camera", moveCamera)
        .writes<scene::Transform>()
        .writes<scene::Camera>()
        .writes<Scroller>()
        .readsResource<platform::Input>()
        .readsResource<scene::Time>();
    level.systems()
        .add(ecs::Phase::Update, "plant_flowers", plantFlowers)
        .reads<Scroller>()
        .reads<scene::Transform>()
        .reads<scene::GlobalTransform>()
        .writes<scene::Tilemap>()
        .readsResource<platform::Input>()
        .after("move_camera");

    f32 sinceTitle = 0.0F;
    const int exitCode =
        app.run(level, {.update = [&](engine::App& running, f32 deltaSeconds) {
                    if (running.input().wasPressed(platform::Key::Escape)) {
                        running.quit();
                    }
                    sinceTitle += deltaSeconds;
                    if (sinceTitle < 0.5F) {
                        return;
                    }
                    sinceTitle = 0.0F;
                    const auto& extraction = level.world().resource<scene::RenderExtraction>();
                    running.window().setTitle(
                        std::format("Teserra tilemap - {:.0f} FPS - chunks: {} drawn, {} culled, {} built",
                                    running.frameTimer().fps(), extraction.packet.tilemapChunks.size(),
                                    extraction.culledChunks, extraction.builtChunks));
                }});
    log::flush();
    return exitCode;
}
