#pragma once

// Scenes own worlds (docs/architecture/ecs.md#scenes-own-worlds).

#include <teserra/ecs/events.hpp>
#include <teserra/ecs/schedule.hpp>
#include <teserra/ecs/world.hpp>
#include <teserra/scene/time.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace teserra::scene {

/// A level, a menu: one World of entities, components and resources, and the Schedule of systems that run
/// on it. Every scene's World has a Time resource, and every scene's Schedule has the engine's transform
/// system (kTransformSystemName, PostUpdate). With the renderer, every scene also has a RenderExtraction
/// resource and the render extraction system (kRenderExtractionSystemName, Extract). Scenes are created by
/// the App's SceneManager, which hands out references; the App runs the active scene's phases every frame.
///
///     scene::Scene& level = app.scenes().create("level");
///     const ecs::Entity hero = level.spawn();
///     level.add<Player>(hero);
///     level.systems().add(ecs::Phase::Update, "move_player", movePlayer).writes<Transform>();
///     return app.run(level);
///
/// A scene can build its content when it becomes active and clear it when it stops being active, with
/// hooks that the SceneManager runs at the frame boundary of a switch (scene_manager.hpp):
///
///     level.onEnter([&](scene::Scene& s) { buildLevel(s); });
///     level.onExit([](scene::Scene& s) { s.reset(); });
///     app.scenes().switchTo(level); // while the level runs: restarts it at the next frame boundary
class Scene {
public:
    explicit Scene(std::string name);
    ~Scene() = default;

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    /// The name given to SceneManager::create().
    [[nodiscard]] std::string_view name() const noexcept { return m_name; }

    /// The scene's entities, components and resources.
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
    /// the first time it is asked for; later calls do nothing. PreUpdate starts the frame of the world's
    /// events first (ecs::updateEvents()). Called by the App for the active scene.
    void run(ecs::Phase phase);

    /// The world's Events<T>, created on first use (ecs::addEvents()): events systems send and read.
    template <typename T>
    ecs::Events<T>& addEvents() {
        return ecs::addEvents<T>(m_world);
    }

    /// Returns the scene to how SceneManager::create() made it, keeping what the game set up once:
    ///  - every entity is destroyed (ecs::World::destroyAll());
    ///  - the built-in state starts over: Time (elapsed time and frame count from 0), the collision state
    ///    (no pairs carried over, so no CollisionEnded for destroyed entities), and the pending events of
    ///    every type are dropped;
    ///  - Startup runs again the next time the scene runs.
    /// The systems and the game's own resources stay; remove those that belong to the content yourself.
    /// Call it while the scene does not run (asserted only through World: not during forEachEntity()),
    /// e.g. from an exit hook or an App callback.
    void reset();

    /// Hooks the SceneManager calls at the frame boundary where a switch takes effect: `onExit` of the scene
    /// that stops being active, then `onEnter` of the one that becomes active. Switching to the active scene
    /// runs both on it: a restart. Neither scene runs during the hooks, so they may create and destroy
    /// entities and resources freely. Setting a hook replaces the previous one; an empty function removes
    /// it.
    void onEnter(std::function<void(Scene&)> hook) { m_onEnter = std::move(hook); }
    void onExit(std::function<void(Scene&)> hook) { m_onExit = std::move(hook); }
    /// Runs the enter or exit hook, if any. Called by the SceneManager.
    void enter();
    void exit();

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
    std::function<void(Scene&)> m_onEnter;
    std::function<void(Scene&)> m_onExit;
    bool m_started = false;
};

} // namespace teserra::scene
