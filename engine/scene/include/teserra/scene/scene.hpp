#pragma once

// Scenes own worlds (docs/architecture/ecs.md#scenes-own-worlds).

#include <teserra/ecs/schedule.hpp>
#include <teserra/ecs/world.hpp>
#include <teserra/scene/time.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace teserra::scene {

/// A level, a menu: one World of entities, components and resources, and the Schedule of systems that run
/// on it. Every scene's World has a Time resource, and every scene's Schedule has the engine's transform
/// system (kTransformSystemName, PostUpdate). Scenes are created by the App's SceneManager, which hands out
/// references; the App runs the active scene's phases every frame.
///
///     scene::Scene& level = app.scenes().create("level");
///     const ecs::Entity hero = level.spawn();
///     level.add<Player>(hero);
///     level.systems().add(ecs::Phase::Update, "move_player", movePlayer).writes<Transform>();
///     return app.run(level);
class Scene {
public:
    explicit Scene(std::string name);
    ~Scene() = default;

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    [[nodiscard]] std::string_view name() const noexcept { return m_name; }

    [[nodiscard]] ecs::World& world() noexcept { return m_world; }
    [[nodiscard]] const ecs::World& world() const noexcept { return m_world; }

    /// A new entity: world().create().
    [[nodiscard]] ecs::Entity spawn() { return m_world.create(); }

    /// Gives `entity` a T: world().add<T>(entity, args...). Returns the component, or nothing for a tag.
    template <ecs::Component T, typename... Args>
    decltype(auto) add(ecs::Entity entity, Args&&... args) {
        return m_world.add<T>(entity, std::forward<Args>(args)...);
    }

    /// The systems of this scene.
    [[nodiscard]] ecs::Schedule& systems() noexcept { return m_schedule; }

    /// Runs the systems of `phase` on this scene's world, then applies their commands. Startup runs only
    /// the first time it is asked for; later calls do nothing. Called by the App for the active scene.
    void run(ecs::Phase phase);

    /// Whether Startup has run.
    [[nodiscard]] bool started() const noexcept { return m_started; }

    /// This scene's Time resource.
    [[nodiscard]] Time& time() { return m_world.resource<Time>(); }
    [[nodiscard]] const Time& time() const { return m_world.resource<Time>(); }

private:
    std::string m_name;
    ecs::World m_world;
    // After the world: the schedule's commands refer to it, so they are destroyed first.
    ecs::Schedule m_schedule;
    bool m_started = false;
};

} // namespace teserra::scene
