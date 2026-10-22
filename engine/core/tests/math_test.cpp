#include <teserra/core/math.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <numbers>

using namespace teserra;
using Catch::Matchers::WithinAbs;

namespace {

constexpr f32 kEpsilon = 1e-5F;

// Clip space -> normalized device coordinates.
Vec3 project(const Mat4& projection, const Vec3& point) {
    const Vec4 clip = projection * Vec4(point, 1.0F);
    return Vec3(clip) / clip.w;
}

void checkNear(const Vec3& actual, const Vec3& expected) {
    CHECK_THAT(actual.x, WithinAbs(expected.x, kEpsilon));
    CHECK_THAT(actual.y, WithinAbs(expected.y, kEpsilon));
    CHECK_THAT(actual.z, WithinAbs(expected.z, kEpsilon));
}

} // namespace

// 3D readiness (ADR-0024): projections map depth to [0, 1] and keep +Y up.
TEST_CASE("math: perspective maps near and far to depth 0 and 1 with +Y up", "[core][math]") {
    const Mat4 projection = math::perspective(math::radians(90.0F), 1.0F, 1.0F, 100.0F);

    checkNear(project(projection, {0.0F, 0.0F, -1.0F}), {0.0F, 0.0F, 0.0F});
    checkNear(project(projection, {0.0F, 0.0F, -100.0F}), {0.0F, 0.0F, 1.0F});
    // With a 90 degree vertical field of view, y == distance is the top edge of the view.
    checkNear(project(projection, {0.0F, 1.0F, -1.0F}), {0.0F, 1.0F, 0.0F});
    checkNear(project(projection, {1.0F, 0.0F, -1.0F}), {1.0F, 0.0F, 0.0F});

    const f32 middleDepth = project(projection, {0.0F, 0.0F, -10.0F}).z;
    CHECK(middleDepth > 0.0F);
    CHECK(middleDepth < 1.0F);
}

TEST_CASE("math: orthographic maps the box to [-1, 1] x [-1, 1] x [0, 1] with +Y up", "[core][math]") {
    const Mat4 projection = math::orthographic(-10.0F, 10.0F, -5.0F, 5.0F, 0.0F, 20.0F);

    checkNear(project(projection, {0.0F, 0.0F, 0.0F}), {0.0F, 0.0F, 0.0F});
    checkNear(project(projection, {0.0F, 0.0F, -20.0F}), {0.0F, 0.0F, 1.0F});
    checkNear(project(projection, {10.0F, 5.0F, -10.0F}), {1.0F, 1.0F, 0.5F});
    checkNear(project(projection, {-10.0F, -5.0F, -10.0F}), {-1.0F, -1.0F, 0.5F});
}

TEST_CASE("math: lookAt puts the target in front of the camera along -Z", "[core][math]") {
    const Mat4 view = math::lookAt({0.0F, 0.0F, 5.0F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});

    checkNear(Vec3(view * Vec4(0.0F, 0.0F, 0.0F, 1.0F)), {0.0F, 0.0F, -5.0F});
    checkNear(Vec3(view * Vec4(0.0F, 1.0F, 0.0F, 1.0F)), {0.0F, 1.0F, -5.0F});
    checkNear(Vec3(view * Vec4(1.0F, 0.0F, 0.0F, 1.0F)), {1.0F, 0.0F, -5.0F});
}

TEST_CASE("math: positive rotation around +Z is counter-clockwise", "[core][math]") {
    const Mat4 rotation = math::toMat4(math::angleAxis(std::numbers::pi_v<f32> / 2.0F, {0.0F, 0.0F, 1.0F}));

    checkNear(Vec3(rotation * Vec4(1.0F, 0.0F, 0.0F, 1.0F)), {0.0F, 1.0F, 0.0F});
    checkNear(Vec3(rotation * Vec4(0.0F, 1.0F, 0.0F, 1.0F)), {-1.0F, 0.0F, 0.0F});
}

TEST_CASE("math: inverse undoes a transform", "[core][math]") {
    const Mat4 transform = math::toMat4(math::angleAxis(0.7F, math::normalize(Vec3(1.0F, 2.0F, 3.0F)))) *
                           math::lookAt({3.0F, 4.0F, 5.0F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
    const Vec3 point{1.5F, -2.0F, 0.25F};

    checkNear(Vec3(math::inverse(transform) * (transform * Vec4(point, 1.0F))), point);
}

TEST_CASE("math: vector functions follow the right-handed conventions", "[core][math]") {
    checkNear(math::cross({1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}), {0.0F, 0.0F, 1.0F});
    CHECK_THAT(math::dot(Vec3(1.0F, 2.0F, 3.0F), Vec3(4.0F, 5.0F, 6.0F)), WithinAbs(32.0F, kEpsilon));
    CHECK_THAT(math::length(math::normalize(Vec3(3.0F, 4.0F, 12.0F))), WithinAbs(1.0F, kEpsilon));
    CHECK_THAT(math::length(Vec2(3.0F, 4.0F)), WithinAbs(5.0F, kEpsilon));
    CHECK_THAT(math::lerp(2.0F, 4.0F, 0.25F), WithinAbs(2.5F, kEpsilon));
    checkNear(math::lerp(Vec3(0.0F), Vec3(2.0F, 4.0F, 8.0F), 0.5F), {1.0F, 2.0F, 4.0F});
    CHECK(math::lerp(Color{0.0F, 0.0F, 0.0F, 0.0F}, Color{1.0F, 0.5F, 0.25F, 1.0F}, 0.5F) ==
          Color{0.5F, 0.25F, 0.125F, 0.5F});
    STATIC_CHECK(math::radians(180.0F) == std::numbers::pi_v<f32>);
}

TEST_CASE("Aabb: is 3D; contains, intersects and merges", "[core][math]") {
    const Aabb box{{0.0F, 0.0F, 0.0F}, {2.0F, 4.0F, 6.0F}};

    checkNear(math::center(box), {1.0F, 2.0F, 3.0F});
    checkNear(math::size(box), {2.0F, 4.0F, 6.0F});
    CHECK(math::contains(box, {1.0F, 1.0F, 1.0F}));
    CHECK(math::contains(box, {2.0F, 4.0F, 6.0F})); // faces are inside
    CHECK_FALSE(math::contains(box, {1.0F, 1.0F, 7.0F}));

    CHECK(math::intersects(box, Aabb{{1.0F, 1.0F, 5.0F}, {3.0F, 3.0F, 9.0F}}));
    CHECK(math::intersects(box, Aabb{{2.0F, 0.0F, 0.0F}, {3.0F, 1.0F, 1.0F}}));       // touching
    CHECK_FALSE(math::intersects(box, Aabb{{0.0F, 0.0F, 6.5F}, {1.0F, 1.0F, 7.0F}})); // apart along z only

    CHECK(math::merge(box, Aabb{{-1.0F, 1.0F, 1.0F}, {1.0F, 5.0F, 2.0F}}) ==
          Aabb{{-1.0F, 0.0F, 0.0F}, {2.0F, 5.0F, 6.0F}});
}

TEST_CASE("Aabb: a 2D box is an Aabb with zero thickness", "[core][math]") {
    const Aabb sprite{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F}};
    const Aabb tile{{0.5F, 0.5F, 0.0F}, {2.0F, 2.0F, 0.0F}};
    const Aabb otherLayer{{0.5F, 0.5F, 1.0F}, {2.0F, 2.0F, 1.0F}};

    CHECK(math::size(sprite).z == 0.0F);
    CHECK(math::intersects(sprite, tile));
    CHECK(math::contains(sprite, {0.5F, 0.5F, 0.0F}));
    CHECK_FALSE(math::intersects(sprite, otherLayer));
    CHECK_FALSE(math::intersects(sprite, Aabb{{1.5F, 0.0F, 0.0F}, {2.0F, 1.0F, 0.0F}}));
}

TEST_CASE("Rect: top-left position and size, right and bottom edges exclusive", "[core][math]") {
    const Rect viewport{{0.0F, 0.0F}, {800.0F, 600.0F}};

    CHECK(math::contains(viewport, {0.0F, 0.0F}));
    CHECK(math::contains(viewport, {799.5F, 599.5F}));
    CHECK_FALSE(math::contains(viewport, {800.0F, 10.0F}));
    CHECK_FALSE(math::contains(viewport, {10.0F, 600.0F}));
}

TEST_CASE("math: glm is configured by teserra_core", "[core][math]") {
#if defined(GLM_FORCE_DEPTH_ZERO_TO_ONE) && defined(GLM_FORCE_RADIANS) && !defined(GLM_FORCE_LEFT_HANDED)
    SUCCEED("depth [0, 1], radians, right-handed");
#else
    FAIL("unexpected glm configuration");
#endif
}
