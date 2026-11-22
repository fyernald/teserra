// Cameras: orthographic and perspective projections of known points (depth [0, 1], +Y up, rows from the
// top), screen/world round trips, Camera2D's zoom, pan and pixel snapping, and the engine shader's camera
// block against CameraData.

#include <teserra/renderer/camera.hpp>

#include <teserra/rhi/shader_blob.hpp>

#include <catch2/catch_test_macros.hpp>
#include <renderer_tests/shaders.hpp>

#include <array>
#include <cmath>
#include <numbers>

using namespace teserra;
using namespace teserra::renderer;

namespace {

bool near(const Vec3& actual, const Vec3& expected, f32 tolerance = 1.0e-3F) {
    return std::abs(actual.x - expected.x) <= tolerance && std::abs(actual.y - expected.y) <= tolerance &&
           std::abs(actual.z - expected.z) <= tolerance;
}

bool near(const Vec2& actual, const Vec2& expected, f32 tolerance = 1.0e-3F) {
    return near(Vec3(actual, 0.0F), Vec3(expected, 0.0F), tolerance);
}

Mat4 translation(const Vec3& offset) {
    Mat4 world{1.0F};
    world[3] = Vec4(offset, 1.0F);
    return world;
}

} // namespace

TEST_CASE("Camera: an orthographic box maps to the viewport, +Y up and rows from the top",
          "[renderer][camera]") {
    // 10 units high on a 2:1 target: 20 units wide, centered on the camera at the origin.
    const CameraData data = cameraData({.projection = Orthographic{.height = 10.0F}}, Mat4{1.0F}, {200, 100});

    CHECK(data.viewportSize == Vec2{200.0F, 100.0F});
    CHECK(near(worldToScreen(data, {0.0F, 0.0F, 0.0F}), {100.0F, 50.0F, 0.5F}));   // planes at -100 and 100
    CHECK(near(worldToScreen(data, {10.0F, 5.0F, 0.0F}), {200.0F, 0.0F, 0.5F}));   // top right
    CHECK(near(worldToScreen(data, {-10.0F, -5.0F, 0.0F}), {0.0F, 100.0F, 0.5F})); // bottom left
    CHECK(near(worldToScreen(data, {0.0F, 0.0F, 100.0F}), {100.0F, 50.0F, 0.0F})); // on the near plane
}

TEST_CASE("Camera: perspective maps the near plane to depth 0 and the far plane to 1", "[renderer][camera]") {
    const Camera camera{
        .projection =
            Perspective{.fovYRadians = std::numbers::pi_v<f32> / 2.0F, .nearPlane = 1.0F, .farPlane = 10.0F}};
    const CameraData data = cameraData(camera, Mat4{1.0F}, {100, 100}); // looking down -Z from the origin

    CHECK(near(worldToScreen(data, {0.0F, 0.0F, -1.0F}), {50.0F, 50.0F, 0.0F}));
    CHECK(near(worldToScreen(data, {0.0F, 0.0F, -10.0F}), {50.0F, 50.0F, 1.0F}));
    CHECK(near(worldToScreen(data, {1.0F, 1.0F, -1.0F}), {100.0F, 0.0F, 0.0F})); // 45 degrees: the top right
    CHECK(near(worldToScreen(data, {5.0F, 0.0F, -5.0F}),
               {100.0F, 50.0F, worldToScreen(data, {0.0F, 0.0F, -5.0F}).z}));
    // Depth grows with distance, between the planes.
    const f32 middle = worldToScreen(data, {0.0F, 0.0F, -5.0F}).z;
    CHECK(middle > 0.0F);
    CHECK(middle < 1.0F);
}

TEST_CASE("Camera: screenToWorld undoes worldToScreen for both projections", "[renderer][camera]") {
    // A moved camera drawing into the right half of a 320x200 target.
    const Rect rightHalf{.position = {0.5F, 0.0F}, .size = {0.5F, 1.0F}};
    const Mat4 world = translation({3.0F, 2.0F, 5.0F});
    const std::array cameras{
        Camera{.projection = Orthographic{.height = 8.0F}, .viewport = rightHalf},
        Camera{.projection = Perspective{.nearPlane = 0.5F, .farPlane = 50.0F}, .viewport = rightHalf},
    };
    const std::array points{Vec3{3.0F, 2.0F, -1.0F}, Vec3{5.5F, -0.25F, -3.0F}, Vec3{0.0F, 4.0F, -10.0F}};

    for (const Camera& camera : cameras) {
        const CameraData data = cameraData(camera, world, {320, 200});
        CHECK(data.viewportSize == Vec2{160.0F, 200.0F});
        CHECK(Vec3(data.position) == Vec3{3.0F, 2.0F, 5.0F});
        for (const Vec3& point : points) {
            const Vec3 screen = worldToScreen(data, point);
            INFO("point " << point.x << ' ' << point.y << ' ' << point.z);
            CHECK(near(screenToWorld(data, Vec2(screen), screen.z), point));
        }
    }
}

TEST_CASE("Camera: renderView carries the camera's order and clear color", "[renderer][camera]") {
    const Camera camera{.order = 3, .clearColor = {.r = 0.25F, .g = 0.5F, .b = 0.75F, .a = 1.0F}};
    const RenderView view = renderView(camera, Mat4{1.0F}, {64, 64}, 1.5F);

    CHECK(view.order == 3);
    CHECK(view.clearColor == camera.clearColor);
    CHECK(view.camera.time == 1.5F);
}

TEST_CASE("Camera2D: each texel is `zoom` pixels, pan and zoom around a point", "[renderer][camera]") {
    Camera2D camera2D{.position = {10.0F, 4.0F}, .zoom = 2.0F, .pixelsPerUnit = 16.0F};
    const UVec2 viewport{800, 600};

    CHECK(std::get<Orthographic>(camera2D.camera(viewport).projection).height == 600.0F / 32.0F);
    CHECK(near(camera2D.worldToScreen({10.0F, 4.0F}, viewport), {400.0F, 300.0F})); // the center
    CHECK(near(camera2D.worldToScreen({11.0F, 5.0F}, viewport), {432.0F, 268.0F})); // one unit: 32 pixels
    // The 2D shortcut agrees with the general camera.
    const CameraData data = cameraData(camera2D.camera(viewport), camera2D.world(), viewport);
    CHECK(near(Vec2(worldToScreen(data, {11.0F, 5.0F, 0.0F})), {432.0F, 268.0F}));

    camera2D.pan({1.0F, 0.0F});
    CHECK(camera2D.position == Vec2{11.0F, 4.0F});

    const Vec2 cursor{600.0F, 150.0F};
    const Vec2 underCursor = camera2D.screenToWorld(cursor, viewport);
    camera2D.zoomAt(2.0F, cursor, viewport);
    CHECK(camera2D.zoom == 4.0F);
    CHECK(near(camera2D.screenToWorld(cursor, viewport), underCursor));
    camera2D.zoomAt(0.0F, cursor, viewport);
    CHECK(camera2D.zoom > 0.0F); // never 0
}

TEST_CASE("Camera2D: pixel snapping rounds the view to whole screen pixels", "[renderer][camera]") {
    Camera2D camera2D{.position = {0.01F, 0.02F}, .zoom = 2.0F, .pixelsPerUnit = 16.0F}; // 32 pixels per unit
    CHECK(camera2D.viewPosition() == camera2D.position);

    camera2D.pixelSnap = true;
    CHECK(near(camera2D.viewPosition(), {0.0F, 1.0F / 32.0F})); // 0.32 and 0.64 pixels round to 0 and 1
    CHECK(near(Vec3(camera2D.world()[3]), {0.0F, 1.0F / 32.0F, 0.0F}));
}

TEST_CASE("Camera: the engine shader's camera block has CameraData's std140 size", "[renderer][camera]") {
    const Result<rhi::ShaderBlobContents> blob = rhi::readShaderBlob(test::shaders::kCameraBlockVert);
    REQUIRE(blob.has_value());
    REQUIRE(blob->reflection.bindings.size() == 1);
    const rhi::ShaderBinding& block = blob->reflection.bindings[0];

    CHECK(block.group == 0);
    CHECK(block.binding == 0);
    CHECK(block.name == "Camera");
    CHECK(block.size == sizeof(CameraData));
}
