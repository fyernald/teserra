#include <teserra/scene/transform.hpp>

#include <teserra/core/assert.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
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

namespace {

// The world through a SystemContext (access checked) or directly (updateGlobalTransforms()).
class ContextAccess {
public:
    explicit ContextAccess(ecs::SystemContext& context) noexcept
        : m_context(&context) {}

    template <typename... Included, typename... Excluded>
    auto view(ecs::Exclude<Excluded...> excluded = {}) {
        return m_context->view<Included...>(excluded);
    }
    template <typename T>
    T* tryGet(ecs::Entity entity) {
        return m_context->tryGet<T>(entity);
    }
    [[nodiscard]] bool isAlive(ecs::Entity entity) const noexcept { return m_context->isAlive(entity); }
    [[nodiscard]] ecs::Commands& commands() noexcept { return m_context->commands(); }

private:
    ecs::SystemContext* m_context;
};

class WorldAccess {
public:
    explicit WorldAccess(ecs::World& world) noexcept
        : m_world(&world) {}

    template <typename... Included, typename... Excluded>
    auto view(ecs::Exclude<Excluded...> excluded = {}) {
        return m_world->view<Included...>(excluded);
    }
    template <typename T>
    T* tryGet(ecs::Entity entity) {
        return m_world->tryGet<std::remove_const_t<T>>(entity);
    }
    [[nodiscard]] bool isAlive(ecs::Entity entity) const noexcept { return m_world->isAlive(entity); }
    // Created on first use: a frame without structural changes makes none.
    [[nodiscard]] ecs::Commands& commands() {
        if (!m_commands) {
            m_commands.emplace(*m_world);
        }
        return *m_commands;
    }
    void apply() {
        if (m_commands) {
            m_commands->apply();
        }
    }

private:
    ecs::World* m_world;
    std::optional<ecs::Commands> m_commands;
};

template <typename Access>
Mat4 localMatrix(Access& access, ecs::Entity entity) {
    const Transform* transform = access.template tryGet<const Transform>(entity);
    return transform != nullptr ? transform->matrix() : Mat4{1.0F};
}

// The world matrix of `entity` from its GlobalTransform when it has a Transform and a GlobalTransform
// (updated earlier this pass: roots first, then by depth), or else from its chain of local transforms (an
// entity whose GlobalTransform is still a pending command).
template <typename Access>
Mat4 worldMatrix(Access& access, ecs::Entity entity) {
    if (access.template tryGet<const Transform>(entity) != nullptr) {
        if (const GlobalTransform* global = access.template tryGet<const GlobalTransform>(entity)) {
            return global->matrix;
        }
    }
    Mat4 matrix = localMatrix(access, entity);
    const Parent* link = access.template tryGet<const Parent>(entity);
    for (u32 depth = 0; link != nullptr && depth < kMaxHierarchyDepth && access.isAlive(link->entity);
         ++depth) {
        matrix = localMatrix(access, link->entity) * matrix;
        link = access.template tryGet<const Parent>(link->entity);
    }
    return matrix;
}

template <typename Access>
void setGlobal(Access& access, ecs::Entity entity, const Mat4& matrix) {
    if (GlobalTransform* global = access.template tryGet<GlobalTransform>(entity)) {
        global->matrix = matrix;
    } else {
        // Commands only record, so adding while a view iterates is fine; it appears at the sync point.
        access.commands().template add<GlobalTransform>(entity, GlobalTransform{.matrix = matrix});
    }
}

template <typename Access>
void propagate(Access& access, TransformHierarchyScratch& scratch) {
    // Roots: the world matrix is the local one.
    for (const auto& [entity, transform] : access.template view<const Transform>(ecs::exclude<Parent>)) {
        setGlobal(access, entity, transform.matrix());
    }

    // Children, ordered by depth so that parents are done first. An entity whose chain reaches an entity that
    // is no longer alive lost an ancestor: it is destroyed, with the rest of that subtree.
    scratch.order.clear();
    for (const auto& [entity, parent] : access.template view<const Parent>()) {
        u32 depth = 0;
        bool orphan = false;
        const Parent* link = &parent;
        while (link != nullptr) {
            if (!access.isAlive(link->entity)) {
                orphan = true;
                break;
            }
            ++depth;
            TESERRA_ASSERT(depth < kMaxHierarchyDepth, "The hierarchy above entity {} has a cycle", entity);
            if (depth >= kMaxHierarchyDepth) {
                break;
            }
            link = access.template tryGet<const Parent>(link->entity);
        }
        if (orphan) {
            access.commands().destroy(entity);
            continue;
        }
        scratch.order.push_back({.depth = depth, .entity = entity});
    }
    std::ranges::sort(scratch.order, {}, &TransformHierarchyScratch::Entry::depth);
    for (const TransformHierarchyScratch::Entry& entry : scratch.order) {
        const Transform* transform = access.template tryGet<const Transform>(entry.entity);
        if (transform == nullptr) {
            continue; // no GlobalTransform; its children see the parent's world matrix through it
        }
        const ecs::Entity parent = access.template tryGet<const Parent>(entry.entity)->entity;
        setGlobal(access, entry.entity, worldMatrix(access, parent) * transform->matrix());
    }

    // Children lists follow the Parent components: stale entries go, missing ones are appended.
    for (auto [entity, children] : access.template view<Children>()) {
        std::erase_if(children.entities, [&](ecs::Entity child) {
            const Parent* link = access.template tryGet<const Parent>(child);
            return link == nullptr || link->entity != entity;
        });
    }
    scratch.missing.clear();
    for (const TransformHierarchyScratch::Entry& entry : scratch.order) {
        const ecs::Entity parent = access.template tryGet<const Parent>(entry.entity)->entity;
        if (Children* children = access.template tryGet<Children>(parent)) {
            if (std::ranges::find(children->entities, entry.entity) == children->entities.end()) {
                children->entities.push_back(entry.entity);
            }
        } else {
            scratch.missing.push_back({.parent = parent, .child = entry.entity});
        }
    }
    // Parents without Children get one, with all their children at once.
    std::ranges::stable_sort(scratch.missing, [](const auto& a, const auto& b) {
        return std::hash<ecs::Entity>{}(a.parent) < std::hash<ecs::Entity>{}(b.parent);
    });
    for (usize first = 0; first < scratch.missing.size();) {
        usize last = first;
        Children children;
        while (last < scratch.missing.size() &&
               scratch.missing[last].parent == scratch.missing[first].parent) {
            children.entities.push_back(scratch.missing[last].child);
            ++last;
        }
        access.commands().template add<Children>(scratch.missing[first].parent, std::move(children));
        first = last;
    }
}

} // namespace

void transformSystem(ecs::SystemContext& context) {
    ContextAccess access(context);
    TransformHierarchyScratch* scratch = context.tryResource<TransformHierarchyScratch>();
    if (scratch != nullptr) {
        propagate(access, *scratch);
    } else {
        TransformHierarchyScratch local;
        propagate(access, local);
    }
}

void updateGlobalTransforms(ecs::World& world) {
    WorldAccess access(world);
    // The scratch memory lives in the world, so later updates reuse it.
    TransformHierarchyScratch* scratch = world.tryResource<TransformHierarchyScratch>();
    if (scratch == nullptr) {
        scratch = &world.insertResource<TransformHierarchyScratch>();
    }
    propagate(access, *scratch);
    access.apply();
}

} // namespace teserra::scene
