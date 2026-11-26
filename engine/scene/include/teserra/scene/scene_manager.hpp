#pragma once

// The scenes of an App and the active one (docs/architecture/ecs.md#scenes-own-worlds).

#include <teserra/core/types.hpp>
#include <teserra/scene/scene.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace teserra::scene {

/// Owns the scenes of an App and knows the active one. A switch takes effect at the next frame boundary,
/// never in the middle of a frame: the App calls beginFrame() before anything else in a frame. Owned by
/// engine::App (App::scenes()); there is no global access.
///
///     scene::Scene& menu = app.scenes().create("menu");
///     scene::Scene& level = app.scenes().create("level");
///     app.scenes().switchTo(level); // from a menu callback: the level runs from the next frame on
///
/// The scene that stops being active is not run again until it is switched to; that is where its exit hook
/// clears it.
class SceneManager {
public:
    SceneManager() = default;
    ~SceneManager() = default;

    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;
    SceneManager(SceneManager&&) = delete;
    SceneManager& operator=(SceneManager&&) = delete;

    /// A new scene named `name`, which must be unique (asserted). References stay valid while the manager
    /// lives.
    Scene& create(std::string name);

    /// The scene named `name`, or nullptr.
    [[nodiscard]] Scene* find(std::string_view name) noexcept;

    /// The scene that runs this frame, or nullptr before the first one is activated.
    [[nodiscard]] Scene* active() noexcept { return m_active; }
    [[nodiscard]] const Scene* active() const noexcept { return m_active; }

    /// Makes `scene`, which must belong to this manager (asserted), the active scene from the next frame
    /// boundary on. The last request before the boundary wins. At the boundary the active scene's exit hook
    /// runs, then `scene`'s enter hook (Scene::onExit(), Scene::onEnter()); switching to the active scene
    /// runs both on it, which restarts it when its exit hook resets it.
    void switchTo(Scene& scene);

    [[nodiscard]] bool hasPendingSwitch() const noexcept { return m_pending != nullptr; }

    /// The frame boundary: applies a pending switch and runs the hooks. Called by the App at the start of
    /// every frame.
    void beginFrame();

    /// Destroys `scene`, which must belong to this manager and be neither active nor the target of a pending
    /// switch (asserted). References to it dangle from now on. Its hooks do not run.
    void destroy(Scene& scene);

    /// Whether `scene` belongs to this manager.
    [[nodiscard]] bool owns(const Scene& scene) const noexcept;

    /// The number of scenes.
    [[nodiscard]] usize size() const noexcept { return m_scenes.size(); }

private:
    std::vector<std::unique_ptr<Scene>> m_scenes;
    Scene* m_active = nullptr;
    Scene* m_pending = nullptr;
};

} // namespace teserra::scene
