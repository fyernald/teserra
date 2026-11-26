// The entity list and inspector (#300): fields show by their FieldType; the selection clears when its entity
// or scene goes away, so nothing reads freed memory; entity fields select their entity; and both windows
// draw a scene's entities and the selected one's components.

#include <teserra/debug_ui/entity_inspector.hpp>

#include <teserra/core/math.hpp>
#include <teserra/debug_ui/debug_ui.hpp>
#include <teserra/ecs/component_meta.hpp>
#include <teserra/ecs/world.hpp>
#include <teserra/scene/collision.hpp>
#include <teserra/scene/scene_manager.hpp>
#include <teserra/scene/transform.hpp>

#include <catch2/catch_test_macros.hpp>
#include <imgui_internal.h>

#include <array>
#include <cstddef>
#include <memory>

using namespace teserra;
using namespace teserra::debugui;

namespace {

// One field of every type.
struct Everything {
    bool flag = true;
    i32 signedValue = -3;
    u32 unsignedValue = 7;
    f32 number = 1.5F;
    Vec2 v2{1.0F, 2.0F};
    Vec3 v3{1.0F, 2.0F, 3.0F};
    Vec4 v4{1.0F, 2.0F, 3.0F, 4.0F};
    Quat rotation = math::angleAxis(math::radians(90.0F), {0.0F, 0.0F, 1.0F});
    Color color{0.5F, 0.25F, 1.0F, 1.0F};
    ecs::Entity target{};
};

} // namespace

TESERRA_COMPONENT(Everything, "Everything");

template <>
struct teserra::ecs::ComponentFields<Everything> {
    static constexpr std::array<FieldDescriptor, 10> kFields{{
        {.name = "flag", .offset = offsetof(Everything, flag), .type = FieldType::Bool},
        {.name = "signedValue", .offset = offsetof(Everything, signedValue), .type = FieldType::I32},
        {.name = "unsignedValue", .offset = offsetof(Everything, unsignedValue), .type = FieldType::U32},
        {.name = "number", .offset = offsetof(Everything, number), .type = FieldType::F32},
        {.name = "v2", .offset = offsetof(Everything, v2), .type = FieldType::Vec2},
        {.name = "v3", .offset = offsetof(Everything, v3), .type = FieldType::Vec3},
        {.name = "v4", .offset = offsetof(Everything, v4), .type = FieldType::Vec4},
        {.name = "rotation", .offset = offsetof(Everything, rotation), .type = FieldType::Quat},
        {.name = "color", .offset = offsetof(Everything, color), .type = FieldType::Color},
        {.name = "target", .offset = offsetof(Everything, target), .type = FieldType::Entity},
    }};
};

namespace {

// Draws the two windows for a few frames.
void draw(DebugUi& debugUi, EntityInspector& inspector, scene::SceneManager& scenes, int frames = 2) {
    for (int frame = 0; frame < frames; ++frame) {
        debugUi.beginFrame({800.0F, 600.0F}, Vec2{1.0F}, 1.0F / 60.0F);
        ImGui::Begin("Entities");
        inspector.drawEntityList(scenes);
        ImGui::End();
        ImGui::Begin("Inspector");
        inspector.drawInspector(scenes);
        ImGui::End();
        debugUi.endFrame();
    }
}

} // namespace

TEST_CASE("formatField: every field type reads as text", "[debug_ui][inspector]") {
    Everything value;
    value.target = ecs::Entity(12, 3);
    const auto& fields = ecs::ComponentFields<Everything>::kFields;
    CHECK(formatField(fields[0], &value) == "true");
    CHECK(formatField(fields[1], &value) == "-3");
    CHECK(formatField(fields[2], &value) == "7");
    CHECK(formatField(fields[3], &value) == "1.5");
    CHECK(formatField(fields[4], &value) == "1, 2");
    CHECK(formatField(fields[5], &value) == "1, 2, 3");
    CHECK(formatField(fields[6], &value) == "1, 2, 3, 4");
    CHECK(formatField(fields[7], &value) == "pitch 0.0, yaw 0.0, roll 90.0 deg");
    CHECK(formatField(fields[8], &value) == "0.5, 0.25, 1, 1");
    CHECK(formatField(fields[9], &value) == "12:3");
}

TEST_CASE("EntityInspector: the selection clears when its entity is destroyed or its scene is gone",
          "[debug_ui][inspector]") {
    scene::SceneManager scenes;
    scene::Scene& level = scenes.create("level");
    const ecs::Entity hero = level.world().create();
    level.world().add<scene::Transform>(hero);

    EntityInspector inspector;
    inspector.select(level, hero);
    inspector.validate(scenes);
    CHECK(inspector.selectedEntity() == hero);

    level.world().destroy(hero);
    inspector.validate(scenes);
    CHECK(inspector.selectedScene() == nullptr);
    CHECK(inspector.selectedEntity().isNull());

    scene::Scene& menu = scenes.create("menu");
    const ecs::Entity button = menu.world().create();
    inspector.select(menu, button);
    scenes.destroy(menu);
    inspector.validate(scenes); // compares the pointer only: the scene is not read
    CHECK(inspector.selectedScene() == nullptr);
}

TEST_CASE("EntityInspector: draws scenes, entities and the selected entity's fields",
          "[debug_ui][inspector]") {
    const std::unique_ptr<DebugUi> debugUi = *DebugUi::create();
    scene::SceneManager scenes;
    scene::Scene& level = scenes.create("level");
    scenes.create("menu");
    scenes.switchTo(level);
    scenes.beginFrame();
    ecs::World& world = level.world();
    const ecs::Entity other = world.create();
    const ecs::Entity hero = world.create();
    world.add<scene::Transform>(hero, scene::Transform{.position = {4.0F, 5.0F, 0.0F}});
    world.add<scene::Collider>(hero, scene::Collider{.size = {0.75F, 0.75F}});
    world.add<Everything>(hero, Everything{.target = other});

    EntityInspector inspector;
    draw(*debugUi, inspector, scenes);
    CHECK(inspector.selectedScene() == nullptr);

    inspector.select(level, hero);
    draw(*debugUi, inspector, scenes);
    CHECK(inspector.selectedEntity() == hero);
    CHECK(debugUi->drawData()->TotalVtxCount > 0);

    // The selected entity destroyed between frames: the inspector draws nothing of it.
    world.destroy(hero);
    draw(*debugUi, inspector, scenes, 1);
    CHECK(inspector.selectedScene() == nullptr);
}
