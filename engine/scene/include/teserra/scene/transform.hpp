#pragma once

// Transforms: 3D even in 2D games (docs/architecture/renderer.md#3d-readiness-checklist, ADR-0024).

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/ecs/component_meta.hpp>
#include <teserra/ecs/system_context.hpp>
#include <teserra/ecs/type_key.hpp>
#include <teserra/ecs/world.hpp>
#include <teserra/scene/hierarchy.hpp>

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

namespace teserra::scene {

/// Where an entity is, relative to its Parent's GlobalTransform or, without one, to the world: position,
/// rotation and scale in 3D. 2D games use the helpers, which work in the XY plane and leave Z alone. World
/// space is right-handed and +Y up; a positive 2D rotation is counter-clockwise around +Z (ADR-0024).
///
///     scene.add<Transform>(hero, Transform{.position = {2.0F, 1.0F, 0.0F}});
///     transform.setRotation2D(math::radians(90.0F)); // facing +Y
struct Transform {
    Vec3 position{0.0F};
    Quat rotation{1.0F, 0.0F, 0.0F, 0.0F}; // identity (w, x, y, z)
    Vec3 scale{1.0F};

    /// The translation, rotation and scale as one matrix: translate * rotate * scale.
    [[nodiscard]] Mat4 matrix() const noexcept;

    [[nodiscard]] Vec2 position2D() const noexcept { return {position.x, position.y}; }
    /// Sets x and y; keeps z (draw order and the 3D depth of a 2D scene).
    void setPosition2D(Vec2 xy) noexcept {
        position.x = xy.x;
        position.y = xy.y;
    }

    /// The rotation around +Z in radians, in (-pi, pi]; counter-clockwise is positive. For a rotation that
    /// is not purely around Z, the angle of its Z part (yaw around Z).
    [[nodiscard]] f32 rotation2D() const noexcept;
    /// Rotates by `radians` around +Z, replacing any 3D rotation.
    void setRotation2D(f32 radians) noexcept;

    [[nodiscard]] Vec2 scale2D() const noexcept { return {scale.x, scale.y}; }
    /// Sets the x and y scale; keeps z.
    void setScale2D(Vec2 xy) noexcept {
        scale.x = xy.x;
        scale.y = xy.y;
    }
};

/// The world matrix of an entity with a Transform, computed by updateGlobalTransforms(). Read it, do not
/// write it: the next update overwrites it.
struct GlobalTransform {
    Mat4 matrix{1.0F};

    /// The world position: the matrix's translation.
    [[nodiscard]] Vec3 translation() const noexcept { return Vec3{matrix[3]}; }
};

/// The name every scene's Schedule gives transformSystem(), for before()/after() constraints.
inline constexpr std::string_view kTransformSystemName = "teserra.transforms";

/// Reused memory of the transform system, so a steady-state frame allocates nothing: a resource of every
/// scene's World.
struct TransformHierarchyScratch {
    struct Entry {
        u32 depth = 0;
        ecs::Entity entity{};
    };
    struct Link {
        ecs::Entity parent{};
        ecs::Entity child{};
    };
    std::vector<Entry> order;
    std::vector<Link> missing;
};

/// The transform system, which every Scene runs in PostUpdate (docs/architecture/ecs.md#transform-
/// hierarchies):
/// - A root (no Parent) gets its Transform's matrix; a child its parent's world matrix times its own, in
///   order of depth, so every parent is done before its children (full 3D matrices).
/// - Entities with a Transform but no GlobalTransform get one with the right matrix through commands, so it
///   exists after PostUpdate's sync point and is correct for Extract.
/// - An entity whose parent (or any ancestor) is no longer alive is destroyed through commands: destroying a
///   parent destroys its subtree at the next PostUpdate.
/// - Children components follow the Parent components.
/// Reads Transform and Parent; writes GlobalTransform, Children and the TransformHierarchyScratch resource.
void transformSystem(ecs::SystemContext& context);

/// What transformSystem() does, directly on a World outside a Schedule, e.g. in tools and tests: new
/// GlobalTransforms (and destroyed orphans) apply at once.
void updateGlobalTransforms(ecs::World& world);

} // namespace teserra::scene

TESERRA_COMPONENT(teserra::scene::Transform, "Transform");
TESERRA_COMPONENT(teserra::scene::GlobalTransform, "GlobalTransform");

template <>
struct teserra::ecs::ComponentFields<teserra::scene::Transform> {
    static constexpr std::array<FieldDescriptor, 3> kFields{{
        {.name = "position",
         .offset = offsetof(teserra::scene::Transform, position),
         .type = FieldType::Vec3},
        {.name = "rotation",
         .offset = offsetof(teserra::scene::Transform, rotation),
         .type = FieldType::Quat},
        {.name = "scale", .offset = offsetof(teserra::scene::Transform, scale), .type = FieldType::Vec3},
    }};
};
