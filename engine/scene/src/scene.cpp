#include <teserra/scene/scene.hpp>

#include <teserra/core/assert.hpp>
#include <teserra/ecs/events.hpp>
#include <teserra/scene/scene_manager.hpp>
#include <teserra/scene/transform.hpp>
#if TESERRA_HAS_RENDERER
#include <teserra/scene/render_extraction.hpp>
#endif

#include <algorithm>
#include <utility>

namespace teserra::scene {

Scene::Scene(std::string name)
    : m_name(std::move(name)) {
    m_world.insertResource<Time>();
    m_world.insertResource<TransformHierarchyScratch>();
    m_schedule.add(ecs::Phase::PostUpdate, std::string(kTransformSystemName), transformSystem)
        .reads<Transform>()
        .reads<Parent>()
        .writes<GlobalTransform>()
        .writes<Children>()
        .writesResource<TransformHierarchyScratch>();
#if TESERRA_HAS_RENDERER
    m_world.insertResource<RenderExtraction>();
    m_schedule.add(ecs::Phase::Extract, std::string(kRenderExtractionSystemName), renderExtractionSystem)
        .reads<Camera>()
        .reads<GlobalTransform>()
        .reads<SpriteRenderer>()
        .reads<TilemapRenderer>()
        .writes<Tilemap>()
        .readsResource<Time>()
        .writesResource<RenderExtraction>();
#endif
}

void Scene::run(ecs::Phase phase) {
    if (phase == ecs::Phase::PreUpdate) {
        ecs::updateEvents(m_world); // the frame starts: last frame's events become the previous ones
    }
    if (phase == ecs::Phase::Startup) {
        if (m_started) {
            return;
        }
        m_started = true;
    }
    m_schedule.run(phase, m_world);
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
