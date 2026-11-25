// Demo game (iteration 14, #104, #105): a small top-down game built only on Teserra's public API. Collect
// every coin in the cave, then reach the door; spikes and slimes send you back to the start. A menu, the
// level and a win screen are scenes that the game switches between.
//
//   Enter           start / play again
//   WASD / arrows   move
//   F1              show the colliders
//   Esc             back to the menu; quit from the menu
//
//   teserra_demo_game                                  until Escape or close
//   teserra_demo_game --frames 600 --hidden-window     smoke run
//
// The game renders at 320x180 and is scaled by whole factors to the window. Its art is drawn in code
// (art.cpp, font.cpp); its rules are components and systems (gameplay.cpp); its screens are scenes
// (flow.cpp).

#include <teserra/assets/asset_registry.hpp>
#include <teserra/core/log.hpp>
#include <teserra/core/result.hpp>
#include <teserra/engine/app.hpp>
#include <teserra/renderer/assets.hpp>
#include <teserra/rhi/sampler.hpp>
#include <teserra/scene/tileset.hpp>

#include "art.hpp"
#include "flow.hpp"
#include "gameplay.hpp"

#include <utility>
#include <vector>

using namespace teserra;

int main(int argc, char** argv) { // NOLINT(bugprone-exception-escape)
    engine::App app{{
        .title = "Teserra demo game",
        .windowSize = {1280, 720},
        .sceneTarget = {.resolution = demo::kResolution,
                        .letterboxColor = {.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F}},
        .spritePass = {.filter = rhi::Filter::Nearest},
        .args = {argc, argv},
    }};
    if (!app.isInitialized()) {
        return 1;
    }

    // The art as a texture asset; the tileset keeps it loaded and marks walls solid.
    const std::vector<std::byte> atlas = demo::drawAtlas();
    rhi::Device& device = app.device();
    const assets::Asset<renderer::Texture> texture =
        app.assets().load<renderer::Texture>("game://atlas", [&] {
            return renderer::Texture::create(device, {demo::kAtlasWidth, demo::kCellSize}, atlas,
                                             rhi::Format::Rgba8Srgb, "Atlas");
        });
    std::vector<scene::TileProperties> properties(demo::kCellCount);
    properties[demo::kWall].solid = true;
    Result<scene::Tileset> tileset = scene::Tileset::grid(
        texture, {.cellSize = {demo::kCellSize, demo::kCellSize}, .count = demo::kCellCount},
        std::move(properties));
    if (!tileset) {
        TESERRA_LOG_ERROR(LogCategory::App, "Cannot cut the atlas: {}", tileset.error());
        return 1;
    }

    demo::Flow flow(app, *tileset);
    const int exitCode = app.run(flow.start(), {.update = [&](engine::App&, f32) { flow.update(); }});
    log::flush();
    return exitCode;
}
