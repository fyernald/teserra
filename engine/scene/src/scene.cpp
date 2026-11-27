#include <teserra/scene/scene.hpp>

#include <teserra/core/assert.hpp>
#include <teserra/core/profile.hpp>
#include <teserra/ecs/events.hpp>
#include <teserra/scene/collision.hpp>
#include <teserra/scene/scene_manager.hpp>
#include <teserra/scene/transform.hpp>
#if TESERRA_HAS_AUDIO
#include <teserra/scene/audio_components.hpp>
#endif
#if TESERRA_HAS_RENDERER
#include <teserra/scene/collision_debug.hpp>
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
    m_world.insertResource<CollisionState>();
    ecs::addEvents<CollisionStarted>(m_world);
    ecs::addEvents<CollisionEnded>(m_world);
    m_schedule.add(ecs::Phase::PostUpdate, std::string(kCollisionSystemName), collisionSystem)
        .reads<Collider>()
        .reads<GlobalTransform>()
        .writesResource<CollisionState>()
        .sendsEvents<CollisionStarted>()
        .sendsEvents<CollisionEnded>()
        .after(kTransformSystemName);
#if TESERRA_HAS_AUDIO
    m_world.insertResource<AudioSourceVoices>();
    m_schedule.add(ecs::Phase::PostUpdate, std::string(kAudioSystemName), audioSystem)
        .reads<AudioSource>()
        .reads<AudioListener>()
        .reads<GlobalTransform>()
        .reads<Transform>()
        .writesResource<AudioSourceVoices>()
        .writesResource<audio::Audio>()
        .after(kTransformSystemName);
#endif
#if TESERRA_HAS_RENDERER
    m_world.insertResource<RenderExtraction>();
    m_world.insertResource<renderer::DebugDrawList>();
    m_world.insertResource<CollisionDebug>();
    m_schedule.add(ecs::Phase::PostUpdate, std::string(kCollisionDebugSystemName), collisionDebugSystem)
        .reads<Collider>()
        .reads<GlobalTransform>()
        .readsResource<CollisionDebug>()
        .readsResource<CollisionState>()
        .writesResource<renderer::DebugDrawList>()
        .after(kCollisionSystemName);
    m_schedule.add(ecs::Phase::Extract, std::string(kRenderExtractionSystemName), renderExtractionSystem)
        .reads<Camera>()
        .reads<GlobalTransform>()
        .reads<SpriteRenderer>()
        .reads<TilemapRenderer>()
        .writes<Tilemap>()
        .readsResource<Time>()
        .writesResource<RenderExtraction>()
        .writesResource<renderer::DebugDrawList>();
#endif
}

void Scene::run(ecs::Phase phase) {
    TESERRA_PROFILE_SCOPE_DYNAMIC(ecs::phaseName(phase));
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

void Scene::reset() {
#if TESERRA_HAS_AUDIO
    stopAudioSources(m_world);
#endif
    m_world.destroyAll();
    m_world.resource<Time>() = Time{};
    m_world.resource<CollisionState>() = CollisionState{};
    // Each update moves the current events to the previous ones and drops the previous ones: after two,
    // none are left.
    ecs::updateEvents(m_world);
    ecs::updateEvents(m_world);
    m_started = false;
}

void Scene::enter() {
    if (m_onEnter) {
        m_onEnter(*this);
    }
}

void Scene::exit() {
#if TESERRA_HAS_AUDIO
    stopAudioSources(m_world);
#endif
    if (m_onExit) {
        m_onExit(*this);
    }
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

void SceneManager::beginFrame() {
    if (m_pending == nullptr) {
        return;
    }
    Scene* const next = std::exchange(m_pending, nullptr);
    if (m_active != nullptr) {
        m_active->exit();
    }
    m_active = next;
    m_active->enter();
}

void SceneManager::destroy(Scene& scene) {
    TESERRA_ASSERT(owns(scene), "Destroying scene '{}', which belongs to another SceneManager", scene.name());
    TESERRA_ASSERT(&scene != m_active, "Destroying scene '{}', which is active", scene.name());
    TESERRA_ASSERT(&scene != m_pending, "Destroying scene '{}', which a switch is pending to", scene.name());
    if (&scene == m_active || &scene == m_pending) {
        return;
    }
    std::erase_if(m_scenes, [&](const auto& owned) { return owned.get() == &scene; });
}

bool SceneManager::owns(const Scene& scene) const noexcept {
    return std::ranges::any_of(m_scenes, [&](const auto& owned) { return owned.get() == &scene; });
}

} // namespace teserra::scene
