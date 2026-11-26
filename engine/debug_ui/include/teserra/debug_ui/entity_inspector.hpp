#pragma once

// The debug UI's entity list and component inspector (#300): the entities of every scene and the components
// of the selected one, with their fields shown from ecs::ComponentMeta, read-only.
//
//   EntityInspector inspector;
//   debugUi.addWindow("Entities", [&] { inspector.drawEntityList(app.scenes()); });
//   debugUi.addWindow("Inspector", [&] { inspector.drawInspector(app.scenes()); });

#include <teserra/core/types.hpp>
#include <teserra/ecs/component_meta.hpp>
#include <teserra/ecs/entity.hpp>
#include <teserra/scene/scene.hpp>
#include <teserra/scene/scene_manager.hpp>

#include <array>
#include <string>
#include <vector>

namespace teserra::debugui {

/// The text a field of a component shows in the inspector: "1.5, 2, 0" for a Vec3, Euler angles in degrees
/// for a Quat, "12:3" for an entity. `data` is the component's memory.
[[nodiscard]] std::string formatField(const ecs::FieldDescriptor& field, const void* data);

/// Selects an entity of a scene and shows it. Main thread; the scenes are read only while drawing.
class EntityInspector {
public:
    /// Draws into the current ImGui window the scenes of `scenes`, the active one first and open, each with
    /// its entities that match the filter (an entity's "index:generation" or a component's name) and the
    /// names of their components. Clicking an entity selects it.
    void drawEntityList(scene::SceneManager& scenes);
    /// Draws into the current ImGui window the selected entity's components: each field by its FieldType
    /// (an entity field is a button that selects it), or the name and size of a component that describes no
    /// fields.
    void drawInspector(scene::SceneManager& scenes);

    /// Selects `entity` of `scene`.
    void select(const scene::Scene& scene, ecs::Entity entity) noexcept;
    /// Clears the selection if its scene left `scenes` or its entity was destroyed, so nothing reads freed
    /// memory. The draw functions call it first.
    void validate(scene::SceneManager& scenes);
    [[nodiscard]] const scene::Scene* selectedScene() const noexcept { return m_scene; }
    [[nodiscard]] ecs::Entity selectedEntity() const noexcept { return m_entity; }

private:
    void drawScene(scene::Scene& scene, bool active);

    const scene::Scene* m_scene = nullptr;
    ecs::Entity m_entity{};
    std::array<char, 64> m_filter{};
    std::vector<ecs::Entity> m_shown; // the entities of the scene being drawn that match the filter
    std::string m_components;         // a row's component names, reused
};

} // namespace teserra::debugui
