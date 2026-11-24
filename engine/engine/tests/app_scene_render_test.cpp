// App with a rendered scene, on the Null backend: the active scene's Input resource follows the events,
// extraction gets the swapchain's size, and without a render callback the App draws the extracted sprites.

#include <teserra/engine/app.hpp>
#include <teserra/platform/key.hpp>
#include <teserra/rhi_null/null_device.hpp>
#include <teserra/scene/render_components.hpp>
#include <teserra/scene/render_extraction.hpp>
#include <teserra/scene/tilemap.hpp>
#include <teserra/scene/tileset.hpp>
#include <teserra/scene/transform.hpp>
#include <teserra/test/allocation_counter.hpp>

#include "headless_platform.hpp"
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace teserra;
using namespace teserra::engine;

namespace {

AppConfig nullConfig(u64 frames) {
    return {.title = "Null scene test",
            .windowSize = {640, 360},
            .backend = rhi::Backend::Null,
            .maxFrames = frames};
}

usize countOf(const std::string& text, const std::string& word) {
    usize count = 0;
    for (usize at = text.find(word); at != std::string::npos; at = text.find(word, at + word.size())) {
        ++count;
    }
    return count;
}

// A white 1x1 texture on the App's device.
rhi::TextureHandle whiteTexture(App& app) {
    const rhi::TextureHandle texture =
        *app.device().createTexture({.width = 1, .height = 1, .debugName = "White"});
    constexpr std::array kWhite{std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
    app.device().writeTexture(texture, {.width = 1, .height = 1}, kWhite);
    return texture;
}

// A camera showing 10 units vertically and `sprites` sprites in a row from x = 0 on, one unit apart.
void buildScene(scene::Scene& level, rhi::TextureHandle texture, int sprites) {
    const ecs::Entity camera = level.spawn();
    level.add<scene::Transform>(camera);
    level.add<scene::Camera>(camera, scene::Camera{.projection = scene::Orthographic{.height = 10.0F}});
    for (int i = 0; i < sprites; ++i) {
        const ecs::Entity sprite = level.spawn();
        level.add<scene::Transform>(sprite, scene::Transform{.position = {static_cast<f32>(i), 0.0F, 0.0F}});
        level.add<scene::SpriteRenderer>(sprite, scene::SpriteRenderer{.texture = texture});
    }
}

} // namespace

TEST_CASE("App: the active scene's sprites are drawn without a render callback",
          "[engine][app][scene][render]") {
    App app(nullConfig(2), std::make_unique<test::HeadlessPlatform>());
    REQUIRE(app.isInitialized());
    scene::Scene& level = app.scenes().create("level");
    buildScene(level, whiteTexture(app), 2);
    // Far outside the view: culled.
    const ecs::Entity far = level.spawn();
    level.add<scene::Transform>(far, scene::Transform{.position = {500.0F, 0.0F, 0.0F}});
    level.add<scene::SpriteRenderer>(far);

    CHECK(app.run(level) == 0);

    const auto& extraction = level.world().resource<scene::RenderExtraction>();
    CHECK(extraction.targetSize == UVec2{640, 360});
    CHECK(extraction.packet.views.size() == 1);
    CHECK(extraction.packet.sprites.size() == 2);
    CHECK(extraction.culledSprites == 1);

    const auto& device = static_cast<rhi::null::NullDevice&>(app.device());
    REQUIRE(device.submitted().size() == 1);
    const std::string commands = rhi::describeCommands(*device.submitted()[0]);
    CHECK(countOf(commands, "DrawIndexed") == 1); // both sprites in one batch
    CHECK(commands.find("BeginRenderPass") != std::string::npos);
}

TEST_CASE("App: the active scene's tilemaps are drawn from cached chunk meshes",
          "[engine][app][scene][render]") {
    App app(nullConfig(3), std::make_unique<test::HeadlessPlatform>());
    REQUIRE(app.isInitialized());
    scene::Scene& level = app.scenes().create("level");
    buildScene(level, whiteTexture(app), 1);
    const rhi::TextureHandle tiles = *app.device().createTexture({.width = 16, .height = 16});
    const assets::Asset<renderer::Texture> texture =
        app.assets().load<renderer::Texture>("game://tiles", [&]() -> Result<renderer::Texture> {
            return renderer::Texture(app.device(), tiles, {16, 16});
        });
    const scene::Tileset tileset = scene::Tileset::grid(texture, {.cellSize = {16, 16}}).value();

    const ecs::Entity map = level.spawn();
    level.add<scene::Transform>(map);
    scene::Tilemap tilemap;
    tilemap.addLayer("ground").setTile({-1, 0}, 1);
    tilemap.layers[0].setTile({0, 0}, 1);
    level.add<scene::Tilemap>(map, std::move(tilemap));
    level.add<scene::TilemapRenderer>(map, scene::TilemapRenderer{.tileset = &tileset});

    CHECK(app.run(level) == 0);
    const auto& extraction = level.world().resource<scene::RenderExtraction>();
    CHECK(extraction.packet.tilemapChunks.size() == 2);
    CHECK(extraction.builtChunks == 0); // built in the first frame, reused since

    const auto& device = static_cast<rhi::null::NullDevice&>(app.device());
    REQUIRE(device.submitted().size() == 1);
    const std::string commands = rhi::describeCommands(*device.submitted()[0]);
    CHECK(countOf(commands, "DrawIndexed") == 3); // two chunks under one sprite batch
}

TEST_CASE("App: a scene without a camera is cleared", "[engine][app][scene][render]") {
    App app(nullConfig(1), std::make_unique<test::HeadlessPlatform>());
    scene::Scene& level = app.scenes().create("level");
    CHECK(app.run(level) == 0);
    const auto& device = static_cast<rhi::null::NullDevice&>(app.device());
    REQUIRE(device.submitted().size() == 1);
    CHECK(countOf(rhi::describeCommands(*device.submitted()[0]), "DrawIndexed") == 0);
}

TEST_CASE("App: the active scene's Input resource follows the events", "[engine][app][scene]") {
    auto platform = std::make_unique<test::HeadlessPlatform>();
    platform->queueEvent(2, platform::KeyChanged{.key = platform::Key::W, .down = true, .repeat = false});
    platform->queueEvent(4, platform::KeyChanged{.key = platform::Key::W, .down = false, .repeat = false});
    App app({.title = "Input test", .backend = std::nullopt, .maxFrames = 4}, std::move(platform));
    scene::Scene& level = app.scenes().create("level");

    std::vector<std::string> seen;
    level.systems()
        .add(ecs::Phase::Update, "read_input",
             [&](ecs::SystemContext& ctx) {
                 const auto& input = ctx.resource<const platform::Input>();
                 seen.emplace_back(input.wasPressed(platform::Key::W)    ? "pressed"
                                   : input.wasReleased(platform::Key::W) ? "released"
                                   : input.isDown(platform::Key::W)      ? "down"
                                                                         : "up");
             })
        .readsResource<platform::Input>();

    CHECK(app.run(level) == 0);
    CHECK(seen == std::vector<std::string>{"up", "pressed", "down", "released"});
    // Without a device there is no target: nothing is extracted.
    CHECK(level.world().resource<scene::RenderExtraction>().targetSize == UVec2{0U});
}

TEST_CASE("App: steady-state frames of a rendered scene do not allocate",
          "[engine][app][scene][render][allocation]") {
    App app(nullConfig(0), std::make_unique<test::HeadlessPlatform>());
    scene::Scene& level = app.scenes().create("level");
    buildScene(level, whiteTexture(app), 100);
    u64 allocations = 0;
    std::unique_ptr<test::AllocationCounter> counter;
    level.systems().add(ecs::Phase::Last, "count_allocations", [&](ecs::SystemContext&) {
        // Frames 1 to 3 warm up (both frame slots); frames 4 to 10 are counted.
        if (app.frameCount() == 2) {
            counter = std::make_unique<test::AllocationCounter>();
        } else if (app.frameCount() == 9) {
            allocations = counter->count();
            app.quit();
        }
    });

    CHECK(app.run(level) == 0);
    CHECK(allocations == 0);
    CHECK(level.world().resource<scene::RenderExtraction>().packet.sprites.size() ==
          10); // x up to 8.9, half a sprite wide: 0 to 9
}
