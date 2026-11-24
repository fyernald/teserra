// Render extraction: cameras become views, visible sprites become draw items, sprites outside every view's
// 3D bounds are culled, and a steady-state Extract of 10 000 sprites allocates nothing.

#include <teserra/scene/render_extraction.hpp>

#include <teserra/renderer/debug_draw.hpp>
#include <teserra/scene/scene.hpp>
#include <teserra/scene/transform.hpp>
#include <teserra/test/allocation_counter.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace teserra;
using namespace teserra::scene;

namespace {

// A camera at the origin showing 10 world units vertically; with a 200 x 100 target, x in [-10, 10] and
// y in [-5, 5].
ecs::Entity addCamera(Scene& scene, Vec3 position = Vec3{0.0F}, i32 order = 0) {
    const ecs::Entity camera = scene.spawn();
    scene.add<Transform>(camera, Transform{.position = position});
    scene.add<Camera>(camera, Camera{.projection = Orthographic{.height = 10.0F}, .order = order});
    return camera;
}

ecs::Entity addSprite(Scene& scene, Vec3 position, SpriteRenderer sprite = {}) {
    const ecs::Entity entity = scene.spawn();
    scene.add<Transform>(entity, Transform{.position = position});
    scene.add<SpriteRenderer>(entity, sprite);
    return entity;
}

// A frame's PostUpdate (GlobalTransforms) and Extract.
RenderExtraction& extract(Scene& scene, UVec2 targetSize = {200U, 100U}) {
    RenderExtraction& extraction = scene.world().resource<RenderExtraction>();
    extraction.targetSize = targetSize;
    scene.run(ecs::Phase::PostUpdate);
    scene.run(ecs::Phase::Extract);
    return extraction;
}

} // namespace

TEST_CASE("Render extraction: every scene has the resource and the system", "[scene][render]") {
    Scene level("level");
    CHECK(level.world().hasResource<RenderExtraction>());
    CHECK(level.systems().order(ecs::Phase::Extract) ==
          std::vector<std::string_view>{kRenderExtractionSystemName});
    CHECK(ecs::componentName<Camera>() == "Camera");
    CHECK(ecs::componentName<SpriteRenderer>() == "SpriteRenderer");
}

TEST_CASE("Render extraction: view bounds of an orthographic camera", "[scene][render]") {
    const Camera camera{.projection = Orthographic{.height = 10.0F, .nearPlane = -1.0F, .farPlane = 3.0F}};
    const Mat4 world = Transform{.position = {4.0F, 2.0F, 0.0F}}.matrix();
    const Aabb bounds = viewBounds(renderer::cameraData(camera, world, UVec2{200U, 100U}));
    CHECK(bounds.min.x == Catch::Approx(-6.0F));
    CHECK(bounds.max.x == Catch::Approx(14.0F));
    CHECK(bounds.min.y == Catch::Approx(-3.0F));
    CHECK(bounds.max.y == Catch::Approx(7.0F));
    // The camera looks down -Z: the near plane is at z = 1, the far one at z = -3.
    CHECK(bounds.min.z == Catch::Approx(-3.0F));
    CHECK(bounds.max.z == Catch::Approx(1.0F));
}

TEST_CASE("Render extraction: view bounds of a perspective camera contain its frustum", "[scene][render]") {
    const Camera camera{
        .projection = Perspective{.fovYRadians = math::radians(90.0F), .nearPlane = 1.0F, .farPlane = 10.0F}};
    const Aabb bounds = viewBounds(renderer::cameraData(camera, Mat4{1.0F}, UVec2{100U, 100U}));
    // 90 degrees: the far plane at z = -10 is 20 units wide and tall.
    CHECK(bounds.min.x == Catch::Approx(-10.0F));
    CHECK(bounds.max.y == Catch::Approx(10.0F));
    CHECK(bounds.min.z == Catch::Approx(-10.0F));
    CHECK(bounds.max.z == Catch::Approx(-1.0F));
}

TEST_CASE("Render extraction: sprite bounds follow the pivot, size and transform", "[scene][render]") {
    SpriteRenderer sprite{.size = {2.0F, 1.0F}, .pivot = {0.0F, 0.0F}}; // pivot at the top-left corner
    Aabb bounds = spriteBounds(sprite, Mat4{1.0F});
    CHECK(bounds.min == Vec3{0.0F, -1.0F, 0.0F});
    CHECK(bounds.max == Vec3{2.0F, 0.0F, 0.0F});

    sprite.pivot = Vec2{0.5F};
    Transform transform{.position = {10.0F, 0.0F, 3.0F}};
    transform.setRotation2D(math::radians(90.0F)); // the 2 x 1 quad stands up: 1 wide, 2 tall
    bounds = spriteBounds(sprite, transform.matrix());
    CHECK(bounds.min.x == Catch::Approx(9.5F));
    CHECK(bounds.max.x == Catch::Approx(10.5F));
    CHECK(bounds.min.y == Catch::Approx(-1.0F));
    CHECK(bounds.max.y == Catch::Approx(1.0F));
    CHECK(bounds.min.z == Catch::Approx(3.0F));
}

TEST_CASE("Render extraction: cameras become views and visible sprites draw items", "[scene][render]") {
    Scene level("level");
    level.time().advance(0.25F, 0.01F);
    addCamera(level, Vec3{0.0F}, 3);
    const rhi::TextureHandle texture{7, 1};
    addSprite(level, {1.0F, 2.0F, 0.0F},
              {.texture = texture,
               .uv = {.min = {0.0F, 0.5F}, .max = {0.5F, 1.0F}},
               .size = {2.0F, 3.0F},
               .pivot = {0.5F, 1.0F},
               .tint = {.r = 1.0F, .g = 0.5F, .b = 0.25F, .a = 1.0F},
               .blend = SpriteBlend::Additive,
               .layer = 10,
               .sortDepth = -2.0F});
    addSprite(level, {50.0F, 0.0F, 0.0F});                    // outside the view
    addSprite(level, {10.4F, 0.0F, 0.0F});                    // 0.1 inside its right edge
    addSprite(level, {0.0F, 0.0F, 0.0F}, {.visible = false}); // hidden
    const ecs::Entity cameraless = level.spawn();             // a camera without a transform
    level.add<Camera>(cameraless);

    const RenderExtraction& extraction = extract(level);
    REQUIRE(extraction.packet.views.size() == 1);
    CHECK(extraction.packet.views[0].order == 3);
    CHECK(extraction.packet.views[0].camera.time == Catch::Approx(0.25F));
    CHECK(extraction.viewBounds.size() == 1);
    CHECK(extraction.culledSprites == 1);
    REQUIRE(extraction.packet.sprites.size() == 2);

    const renderer::SpriteDrawItem& item = extraction.packet.sprites[0];
    CHECK(item.world == Transform{.position = {1.0F, 2.0F, 0.0F}}.matrix());
    CHECK(item.texture == texture);
    CHECK(item.uv.min == Vec2{0.0F, 0.5F});
    CHECK(item.size == Vec2{2.0F, 3.0F});
    CHECK(item.pivot == Vec2{0.5F, 1.0F});
    CHECK(item.tint.g == 0.5F);
    CHECK(item.blend == SpriteBlend::Additive);
    CHECK(item.layer == 10);
    CHECK(item.sortDepth == -2.0F);

    // The next Extract starts over: hiding the first sprite leaves only the one at the edge.
    for (auto [entity, sprite] : level.world().view<SpriteRenderer>()) {
        if (sprite.texture == texture) {
            sprite.visible = false;
        }
    }
    const RenderExtraction& next = extract(level);
    CHECK(next.packet.views.size() == 1);
    CHECK(next.packet.sprites.size() == 1);
    CHECK(next.culledSprites == 1);
}

TEST_CASE("Render extraction: a sprite seen by any camera is kept", "[scene][render]") {
    Scene level("level");
    addCamera(level);
    addCamera(level, {100.0F, 0.0F, 0.0F}, 1);
    addSprite(level, {100.0F, 0.0F, 0.0F}); // only the second camera sees it
    addSprite(level, {50.0F, 0.0F, 0.0F});  // neither does

    const RenderExtraction& extraction = extract(level);
    CHECK(extraction.packet.views.size() == 2);
    CHECK(extraction.packet.sprites.size() == 1);
    CHECK(extraction.culledSprites == 1);
}

TEST_CASE("Render extraction: nothing without a target or a camera", "[scene][render]") {
    Scene level("level");
    addSprite(level, {0.0F, 0.0F, 0.0F});
    SECTION("no camera") {
        const RenderExtraction& extraction = extract(level);
        CHECK(extraction.packet.views.empty());
        CHECK(extraction.packet.sprites.empty());
    }
    SECTION("a zero-sized target, e.g. a minimized window") {
        addCamera(level);
        CHECK(extract(level).packet.sprites.size() == 1);
        const RenderExtraction& extraction = extract(level, UVec2{0U, 100U});
        CHECK(extraction.packet.views.empty());
        CHECK(extraction.packet.sprites.empty());
    }
}

TEST_CASE("Render extraction: entities spawned this frame are drawn this frame", "[scene][render]") {
    Scene level("level");
    addCamera(level);
    level.systems().add(ecs::Phase::Update, "spawn", [](ecs::SystemContext& ctx) {
        const ecs::Entity entity = ctx.commands().create();
        ctx.commands().add<Transform>(entity);
        ctx.commands().add<SpriteRenderer>(entity);
    });
    level.run(ecs::Phase::Update);
    CHECK(extract(level).packet.sprites.size() == 1);
}

TEST_CASE("Render extraction: 10 000 sprites in steady state allocate nothing",
          "[scene][render][allocation]") {
    Scene level("level");
    addCamera(level);
    constexpr int kSide = 100; // a 100 x 100 grid over [-10, 10] x [-10, 10]: half is outside the view
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            addSprite(level,
                      {(static_cast<f32>(x) * 0.2F) - 10.0F, (static_cast<f32>(y) * 0.2F) - 10.0F, 0.0F},
                      {.size = Vec2{0.1F}});
        }
    }
    static_cast<void>(extract(level)); // warm-up: GlobalTransforms are added, vectors grow
    static_cast<void>(extract(level));

    u64 allocations = 0;
    {
        const test::AllocationCounter counter;
        static_cast<void>(extract(level));
        allocations = counter.count();
    }
    CHECK(allocations == 0);
    const RenderExtraction& extraction = level.world().resource<RenderExtraction>();
    CHECK(extraction.packet.sprites.size() + extraction.culledSprites == usize{kSide} * kSide);
    CHECK(extraction.culledSprites > 4000);
}

TEST_CASE("Render extraction: debug lines drawn by systems go into the packet for one frame",
          "[scene][render][debug]") {
    Scene level("level");
    addCamera(level);
    level.systems()
        .add(ecs::Phase::Update, "draw_debug",
             [](ecs::SystemContext& ctx) {
                 ctx.resource<renderer::DebugDrawList>().cross(
                     {0.0F, 0.0F, 0.0F}, 1.0F, Color{.r = 0.0F, .g = 1.0F, .b = 0.0F, .a = 1.0F});
             })
        .writesResource<renderer::DebugDrawList>();
    level.run(ecs::Phase::Update);

    const RenderExtraction& extraction = extract(level);
    CHECK(extraction.packet.debugLines.size() == (renderer::kDebugDrawCompiled ? 6U : 0U));
    CHECK(level.world().resource<renderer::DebugDrawList>().vertices().empty()); // cleared for the next frame

    CHECK(extract(level).packet.debugLines.empty()); // nothing drawn this frame

    // Without a target (minimized), the lines are dropped, not kept for later.
    level.run(ecs::Phase::Update);
    CHECK(extract(level, UVec2{0U}).packet.debugLines.empty());
    CHECK(level.world().resource<renderer::DebugDrawList>().vertices().empty());
}
