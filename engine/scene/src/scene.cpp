#include <teserra/scene/scene.hpp>

#include <teserra/core/assert.hpp>
#include <teserra/scene/scene_manager.hpp>

#include <algorithm>
#include <utility>

namespace teserra::scene {

Scene::Scene(std::string name)
    : m_name(std::move(name)) {
    m_world.insertResource<Time>();
}

Scene& SceneManager::create(std::string name) {
    TESERRA_ASSERT(find(name) == nullptr, "A scene named '{}' already exists", name);
    m_scenes.push_back(std::make_unique<Scene>(std::move(name)));
    return *m_scenes.back();
}

Scene* SceneManager::find(std::string_view name) noexcept {
    const auto found =
        std::ranges::find_if(m_scenes, [&](const auto& scene) { return scene->name() == name; });
    return found != m_scenes.end() ? found->get() : nullptr;
}

void SceneManager::switchTo(Scene& scene) {
    TESERRA_ASSERT(owns(scene), "Switching to scene '{}', which belongs to another SceneManager",
                   scene.name());
    m_pending = &scene;
}

void SceneManager::beginFrame() noexcept {
    if (m_pending != nullptr) {
        m_active = m_pending;
        m_pending = nullptr;
    }
}

bool SceneManager::owns(const Scene& scene) const noexcept {
    return std::ranges::any_of(m_scenes, [&](const auto& owned) { return owned.get() == &scene; });
}

} // namespace teserra::scene
