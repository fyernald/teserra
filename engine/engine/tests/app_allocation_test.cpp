// The allocation rule for a whole engine frame (#112,
// docs/architecture/memory-and-performance.md#verifying-the- allocation-rule): an App on the null backend
// runs a scene with a two-layer tilemap over several chunks, sprites with colliders that a moving player
// overlaps (collision events every frame), a camera that is a child of the player, debug lines and the
// collider debug view, and a system that destroys and respawns an entity every frame. After warm-up, a frame
// allocates nothing: events, extraction, culling, batching, passes and command recording all reuse their
// memory. A second test checks that the counter sees an allocation a system makes, so the first one cannot
// pass by counting nothing.

#include <teserra/engine/app.hpp>
#include <teserra/renderer/assets.hpp>
#include <teserra/renderer/debug_draw.hpp>
#include <teserra/scene/collision.hpp>
#include <teserra/scene/collision_debug.hpp>
#include <teserra/scene/hierarchy.hpp>
#include <teserra/scene/render_components.hpp>
#include <teserra/scene/render_extraction.hpp>
#include <teserra/scene/tilemap.hpp>
#include <teserra/scene/tileset.hpp>
#include <teserra/scene/time.hpp>
#include <teserra/scene/transform.hpp>
#include <teserra/test/allocation_counter.hpp>

#include "headless_platform.hpp"
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

using namespace teserra;
using namespace teserra::engine;

namespace {

struct Mover {
    f32 phase = 0.0F;
};
struct Respawner {};

} // namespace

TESERRA_COMPONENT(Mover, "Mover");
TESERRA_COMPONENT(Respawner, "Respawner");

namespace {

constexpr u64 kWarmUpFrames = 12; // both frame slots, events' double buffers, culling and batching grown
constexpr u64 kCountedFrames = 30;

// What the busy scene keeps alive while the App runs.
struct BusyScene {
    assets::Asset<renderer::Texture> atlas;
    std::optional<scene::Tileset> tileset;
    ecs::Entity player{};
};

// Builds the busy scene in `level`.
void buildBusyScene(App& app, scene::Scene& level, BusyScene& keep) {
    const std::vector<std::byte> texels(usize{16} * 8 * 4, std::byte{200});
    keep.atlas = app.assets().load<renderer::Texture>(
        "test://atlas", [&] { return renderer::Texture::create(app.device(), {16, 8}, texels); });
    keep.tileset = *scene::Tileset::grid(keep.atlas, {.cellSize = {8, 8}, .count = 2});

    // A 48x40 map over four 32x32 chunks, two layers.
    scene::Tilemap map;
    // addLayer() references are valid until the next addLayer(): add both, then take them.
    map.addLayer("ground");
    map.addLayer("decor");
    scene::TilemapLayer& ground = map.layers[0];
    scene::TilemapLayer& decor = map.layers[1];
    for (int y = -20; y < 20; ++y) {
        for (int x = -24; x < 24; ++x) {
            ground.setTile({x, y}, 1);
            if ((x + y) % 5 == 0) {
                decor.setTile({x, y}, 2);
            }
        }
    }
    const ecs::Entity tiles = level.spawn();
    level.add<scene::Transform>(tiles);
    level.add<scene::Tilemap>(tiles, std::move(map));
    level.add<scene::TilemapRenderer>(tiles, scene::TilemapRenderer{.tileset = &*keep.tileset});

    // Sprites with colliders in a band the player sweeps through.
    const rhi::TextureHandle sprites = keep.atlas.get().handle();
    for (int i = 0; i < 150; ++i) {
        const ecs::Entity coin = level.spawn();
        level.add<scene::Transform>(coin,
                                    scene::Transform{.position = {static_cast<f32>(i % 30) - 15.0F,
                                                                  static_cast<f32>(i / 30) - 2.0F, 0.0F}});
        level.add<scene::SpriteRenderer>(coin, scene::SpriteRenderer{.texture = sprites, .layer = 5});
        level.add<scene::Collider>(coin, scene::Collider{.size = Vec2{0.5F}});
        if (i == 0) {
            level.add<Respawner>(coin);
        }
    }

    keep.player = level.spawn();
    level.add<scene::Transform>(keep.player);
    level.add<scene::SpriteRenderer>(keep.player, scene::SpriteRenderer{.texture = sprites, .layer = 10});
    level.add<scene::Collider>(keep.player, scene::Collider{.size = Vec2{1.5F}});
    level.add<Mover>(keep.player);

    // The camera follows the player as its child.
    const ecs::Entity camera = level.spawn();
    level.add<scene::Transform>(camera);
    level.add<scene::Camera>(camera, scene::Camera{.projection = scene::Orthographic{.height = 18.0F}});
    scene::setParent(level.world(), camera, keep.player);

    level.world().resource<scene::CollisionDebug>().enabled = true;
    level.addEvents<scene::CollisionStarted>();

    // The player sweeps left and right over the coins: collisions start and end every few frames, while the
    // camera stays over the same chunks.
    level.systems()
        .add(ecs::Phase::Update, "move",
             [](ecs::SystemContext& ctx) {
                 const f32 dt = ctx.resource<const scene::Time>().deltaSeconds();
                 for (auto [entity, transform, mover] : ctx.view<scene::Transform, Mover>()) {
                     mover.phase += dt * 3.0F;
                     transform.setPosition2D({std::sin(mover.phase) * 3.0F, 0.0F});
                 }
             })
        .writes<scene::Transform>()
        .writes<Mover>()
        .readsResource<scene::Time>();
    // Reads the collision events, as gameplay does.
    level.systems()
        .add(ecs::Phase::PostUpdate, "count_hits",
             [](ecs::SystemContext& ctx) {
                 usize hits = 0;
                 ctx.readEvents<scene::CollisionStarted>([&](const scene::CollisionStarted&) { ++hits; });
                 static_cast<void>(hits);
             })
        .readsEvents<scene::CollisionStarted>()
        .after(scene::kCollisionSystemName);
    // Destroys one entity and spawns its replacement every frame, through commands.
    level.systems()
        .add(ecs::Phase::Update, "respawn",
             [](ecs::SystemContext& ctx) {
                 for (auto [entity, transform, sprite, collider, respawner] :
                      ctx.view<const scene::Transform, const scene::SpriteRenderer, const scene::Collider,
                               const Respawner>()) {
                     ctx.commands().destroy(entity);
                     const ecs::Entity next = ctx.commands().create();
                     ctx.commands().add<scene::Transform>(next, transform);
                     ctx.commands().add<scene::SpriteRenderer>(next, sprite);
                     ctx.commands().add<scene::Collider>(next, collider);
                     ctx.commands().add<Respawner>(next);
                 }
             })
        .reads<scene::Transform>()
        .reads<scene::SpriteRenderer>()
        .reads<scene::Collider>()
        .reads<Respawner>();
    // Debug lines every frame, as a game's debug view does.
    level.systems()
        .add(ecs::Phase::PostUpdate, "debug_lines",
             [](ecs::SystemContext& ctx) {
                 auto& debug = ctx.resource<renderer::DebugDrawList>();
                 debug.rect({.position = Vec2{-4.0F}, .size = Vec2{8.0F}},
                            {.r = 1.0F, .g = 1.0F, .b = 0.0F, .a = 1.0F});
                 debug.circle({0.0F, 0.0F, 0.0F}, 2.0F, {.r = 0.0F, .g = 1.0F, .b = 1.0F, .a = 1.0F});
             })
        .writesResource<renderer::DebugDrawList>();
}

// Runs the busy scene and returns the allocations of the counted frames; `extra` adds a system.
u64 countFrameAllocations(const std::function<void(scene::Scene&)>& extra = {}) {
    App app({.title = "Allocation test", .windowSize = {640, 360}, .backend = rhi::Backend::Null},
            std::make_unique<test::HeadlessPlatform>());
    REQUIRE(app.isInitialized());
    scene::Scene& level = app.scenes().create("level");
    BusyScene keep;
    buildBusyScene(app, level, keep);
    if (extra) {
        extra(level);
    }

    u64 allocations = 0;
    std::unique_ptr<test::AllocationCounter> counter;
    level.systems().add(ecs::Phase::Last, "count_allocations", [&](ecs::SystemContext&) {
        if (app.frameCount() == kWarmUpFrames) {
            counter = std::make_unique<test::AllocationCounter>();
        } else if (app.frameCount() == kWarmUpFrames + kCountedFrames) {
            allocations = counter->count();
            app.quit();
        }
    });
    CHECK(app.run(level) == 0);

    // The scene really was busy: chunks, sprites, collisions and debug lines all reached the frame.
    const auto& extraction = level.world().resource<scene::RenderExtraction>();
    CHECK(extraction.packet.tilemapChunks.size() >= 4);
    CHECK(extraction.packet.sprites.size() > 50);
    CHECK_FALSE(extraction.packet.debugLines.empty());
    CHECK_FALSE(level.world().resource<scene::CollisionState>().overlapping.empty());
    return allocations;
}

} // namespace

TEST_CASE("App: a steady-state frame of a busy scene allocates nothing", "[engine][app][allocation]") {
    CHECK(countFrameAllocations() == 0);
}

TEST_CASE("App: the frame allocation test sees an allocation in a system", "[engine][app][allocation]") {
    // A deliberate allocation in a hot path, as a regression would add one.
    const u64 allocations = countFrameAllocations([](scene::Scene& level) {
        level.systems().add(ecs::Phase::Update, "leaky", [](ecs::SystemContext&) {
            const auto scratch = std::make_unique<std::vector<int>>(16);
            static_cast<void>(scratch);
        });
    });
    CHECK(allocations >= kCountedFrames);
}
