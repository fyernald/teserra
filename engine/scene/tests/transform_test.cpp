// Transform and GlobalTransform: the TRS matrix, 2D helpers with counter-clockwise angles around +Z
// (ADR-0024), the transform system, and the field descriptions.

#include <teserra/scene/transform.hpp>

#include <teserra/test/allocation_counter.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <numbers>
#include <type_traits>

using namespace teserra;
using namespace teserra::scene;

namespace {

constexpr f32 kPi = std::numbers::pi_v<f32>;

// Where `transform` maps `point`.
Vec3 apply(const Mat4& matrix, Vec3 point) {
    const Vec4 mapped = matrix * Vec4{point, 1.0F};
    return {mapped.x, mapped.y, mapped.z};
}

void checkNear(Vec3 actual, Vec3 expected) {
    CHECK(actual.x == Catch::Approx(expected.x).margin(1e-5));
    CHECK(actual.y == Catch::Approx(expected.y).margin(1e-5));
    CHECK(actual.z == Catch::Approx(expected.z).margin(1e-5));
}

} // namespace

TEST_CASE("Transform: 3D storage and a translate * rotate * scale matrix", "[scene][transform]") {
    // 3D readiness #1: Vec3 / Quat / Vec3, world matrices are Mat4.
    STATIC_CHECK(std::is_same_v<decltype(Transform::position), Vec3>);
    STATIC_CHECK(std::is_same_v<decltype(Transform::rotation), Quat>);
    STATIC_CHECK(std::is_same_v<decltype(Transform::scale), Vec3>);
    STATIC_CHECK(std::is_same_v<decltype(GlobalTransform::matrix), Mat4>);

    CHECK(Transform{}.matrix() == Mat4{1.0F});

    Transform transform{.position = {10.0F, 20.0F, 30.0F}, .scale = {2.0F, 3.0F, 4.0F}};
    transform.setRotation2D(kPi / 2.0F);
    // (1, 0, 0) scales to (2, 0, 0), turns counter-clockwise to (0, 2, 0), moves to (10, 22, 30).
    checkNear(apply(transform.matrix(), {1.0F, 0.0F, 0.0F}), {10.0F, 22.0F, 30.0F});
    // (0, 1, 1) scales to (0, 3, 4), turns to (-3, 0, 4).
    checkNear(apply(transform.matrix(), {0.0F, 1.0F, 1.0F}), {7.0F, 20.0F, 34.0F});
}

TEST_CASE("Transform: 2D rotation angles round-trip, counter-clockwise around +Z", "[scene][transform]") {
    Transform transform;
    CHECK(transform.rotation2D() == 0.0F);
    for (int step = -179; step <= 180; step += 7) {
        const f32 angle = math::radians(static_cast<f32>(step));
        transform.setRotation2D(angle);
        INFO("degrees " << step);
        CHECK(transform.rotation2D() == Catch::Approx(angle).margin(1e-5));
    }
    transform.setRotation2D(math::radians(270.0F)); // wraps to (-pi, pi]
    CHECK(transform.rotation2D() == Catch::Approx(-kPi / 2.0F).margin(1e-5));

    transform.setRotation2D(math::radians(90.0F)); // +X turns to +Y
    checkNear(apply(transform.matrix(), {1.0F, 0.0F, 0.0F}), {0.0F, 1.0F, 0.0F});

    // A 3D rotation: its angle around Z, with a tilt around X added after it.
    transform.rotation =
        math::angleAxis(0.7F, Vec3{0.0F, 0.0F, 1.0F}) * math::angleAxis(0.4F, Vec3{1.0F, 0.0F, 0.0F});
    CHECK(transform.rotation2D() == Catch::Approx(0.7F).margin(1e-5));
}

TEST_CASE("Transform: 2D position and scale helpers keep z", "[scene][transform]") {
    Transform transform{.position = {1.0F, 2.0F, 5.0F}, .scale = {1.0F, 1.0F, 3.0F}};
    transform.setPosition2D({7.0F, -4.0F});
    transform.setScale2D({2.0F, 0.5F});
    CHECK(transform.position2D() == Vec2{7.0F, -4.0F});
    CHECK(transform.position == Vec3{7.0F, -4.0F, 5.0F});
    CHECK(transform.scale2D() == Vec2{2.0F, 0.5F});
    CHECK(transform.scale == Vec3{2.0F, 0.5F, 3.0F});
}

TEST_CASE("updateGlobalTransforms: world matrices for every entity with a Transform", "[scene][transform]") {
    ecs::World world;
    const ecs::Entity hero = world.create();
    world.add<Transform>(hero, Transform{.position = {1.0F, 2.0F, 0.0F}});
    const ecs::Entity marker = world.create(); // no Transform: no GlobalTransform

    updateGlobalTransforms(world);
    REQUIRE(world.has<GlobalTransform>(hero));
    CHECK_FALSE(world.has<GlobalTransform>(marker));
    checkNear(world.get<GlobalTransform>(hero).translation(), {1.0F, 2.0F, 0.0F});

    world.get<Transform>(hero).setPosition2D({5.0F, 6.0F});
    const ecs::Entity late = world.create();
    world.add<Transform>(late, Transform{.position = {0.0F, 0.0F, -3.0F}});
    updateGlobalTransforms(world);
    checkNear(world.get<GlobalTransform>(hero).translation(), {5.0F, 6.0F, 0.0F});
    checkNear(world.get<GlobalTransform>(late).translation(), {0.0F, 0.0F, -3.0F});

    // Steady state: no new entities, no allocations.
    u64 allocations = 0;
    {
        const test::AllocationCounter counter;
        updateGlobalTransforms(world);
        allocations = counter.count();
    }
    CHECK(allocations == 0);
}

TEST_CASE("Transform: fields described for the editor and serialization", "[scene][transform]") {
    const ecs::ComponentMeta& meta = ecs::componentMeta<Transform>();
    REQUIRE(meta.fields.size() == 3);
    CHECK(meta.fields[0].name == "position");
    CHECK(meta.fields[0].type == ecs::FieldType::Vec3);
    CHECK(meta.fields[1].name == "rotation");
    CHECK(meta.fields[1].type == ecs::FieldType::Quat);
    CHECK(meta.fields[1].offset == offsetof(Transform, rotation));
    CHECK(meta.fields[2].name == "scale");
}
