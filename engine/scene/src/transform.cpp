#include <teserra/scene/transform.hpp>

#include <cmath>
#include <vector>

namespace teserra::scene {

Mat4 Transform::matrix() const noexcept {
    // translate * rotate * scale: scale the rotation's columns, then put the position in the last one.
    Mat4 result = math::toMat4(rotation);
    result[0] *= scale.x;
    result[1] *= scale.y;
    result[2] *= scale.z;
    result[3] = Vec4{position, 1.0F};
    return result;
}

f32 Transform::rotation2D() const noexcept {
    // The yaw around Z of a unit quaternion (w, x, y, z); 2 * atan2(z, w) for a rotation purely around Z.
    const f32 sine = 2.0F * ((rotation.w * rotation.z) + (rotation.x * rotation.y));
    const f32 cosine = 1.0F - (2.0F * ((rotation.y * rotation.y) + (rotation.z * rotation.z)));
    return std::atan2(sine, cosine);
}

void Transform::setRotation2D(f32 radians) noexcept {
    rotation = math::angleAxis(radians, Vec3{0.0F, 0.0F, 1.0F});
}

void updateGlobalTransforms(ecs::World& world) {
    // Entities that got a Transform since the last update get a GlobalTransform first. Adding to the pool
    // while the view locks it would assert, so they are collected; this allocates only in frames with such
    // entities.
    // The check comes first because MSVC's debug std::vector allocates even when empty.
    bool anyMissing = false;
    {
        const auto pending = world.view<const Transform>(ecs::exclude<GlobalTransform>);
        anyMissing = pending.begin() != pending.end();
    }
    if (anyMissing) {
        std::vector<ecs::Entity> missing;
        for (const auto& [entity, transform] : world.view<const Transform>(ecs::exclude<GlobalTransform>)) {
            missing.push_back(entity);
        }
        for (const ecs::Entity entity : missing) {
            world.add<GlobalTransform>(entity);
        }
    }

    for (auto [entity, transform, global] : world.view<const Transform, GlobalTransform>()) {
        global.matrix = transform.matrix();
    }
}

} // namespace teserra::scene
