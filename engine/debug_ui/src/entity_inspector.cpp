#include <teserra/debug_ui/entity_inspector.hpp>

#include <teserra/core/math.hpp>
#include <teserra/ecs/world.hpp>

#include <imgui.h>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <string_view>

namespace teserra::debugui {

namespace {

template <typename T>
[[nodiscard]] T readField(const ecs::FieldDescriptor& field, const void* data) noexcept {
    T value{};
    std::memcpy(&value, static_cast<const std::byte*>(data) + field.offset, sizeof(T));
    return value;
}

[[nodiscard]] bool containsIgnoringCase(std::string_view text, std::string_view part) noexcept {
    if (part.empty()) {
        return true;
    }
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    for (usize start = 0; start + part.size() <= text.size(); ++start) {
        usize matched = 0;
        while (matched < part.size() && lower(text[start + matched]) == lower(part[matched])) {
            ++matched;
        }
        if (matched == part.size()) {
            return true;
        }
    }
    return false;
}

// An angle in degrees as shown with one decimal: what would read "-0.0" reads "0.0".
[[nodiscard]] f32 shownDegrees(f32 radians) noexcept {
    const f32 degrees = math::degrees(radians);
    return std::abs(degrees) < 0.05F ? 0.0F : degrees;
}

// "index:generation", the way entities are formatted elsewhere.
void formatEntity(ecs::Entity entity, std::array<char, 32>& out) noexcept {
    const auto result = std::format_to_n(out.data(), out.size() - 1, "{}", entity);
    *result.out = '\0';
}

} // namespace

std::string formatField(const ecs::FieldDescriptor& field, const void* data) {
    switch (field.type) {
        case ecs::FieldType::Bool:
            return readField<bool>(field, data) ? "true" : "false";
        case ecs::FieldType::I32:
            return std::format("{}", readField<i32>(field, data));
        case ecs::FieldType::U32:
            return std::format("{}", readField<u32>(field, data));
        case ecs::FieldType::F32:
            return std::format("{:g}", readField<f32>(field, data));
        case ecs::FieldType::Vec2: {
            const auto v = readField<Vec2>(field, data);
            return std::format("{:g}, {:g}", v.x, v.y);
        }
        case ecs::FieldType::Vec3: {
            const auto v = readField<Vec3>(field, data);
            return std::format("{:g}, {:g}, {:g}", v.x, v.y, v.z);
        }
        case ecs::FieldType::Vec4: {
            const auto v = readField<Vec4>(field, data);
            return std::format("{:g}, {:g}, {:g}, {:g}", v.x, v.y, v.z, v.w);
        }
        case ecs::FieldType::Quat: {
            const Vec3 angles = math::eulerAngles(readField<Quat>(field, data));
            return std::format("pitch {:.1f}, yaw {:.1f}, roll {:.1f} deg", shownDegrees(angles.x),
                               shownDegrees(angles.y), shownDegrees(angles.z));
        }
        case ecs::FieldType::Color: {
            const auto c = readField<Color>(field, data);
            return std::format("{:.3g}, {:.3g}, {:.3g}, {:.3g}", c.r, c.g, c.b, c.a);
        }
        case ecs::FieldType::Entity:
            return std::format("{}", readField<ecs::Entity>(field, data));
    }
    return "?";
}

void EntityInspector::select(const scene::Scene& scene, ecs::Entity entity) noexcept {
    m_scene = &scene;
    m_entity = entity;
}

void EntityInspector::validate(scene::SceneManager& scenes) {
    if (m_scene == nullptr) {
        return;
    }
    if (!scenes.owns(*m_scene) || !m_scene->world().isAlive(m_entity)) {
        m_scene = nullptr;
        m_entity = {};
    }
}

void EntityInspector::drawEntityList(scene::SceneManager& scenes) {
    validate(scenes);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##filter", "Filter: index:generation or component", m_filter.data(),
                             m_filter.size());
    scene::Scene* const active = scenes.active();
    if (active != nullptr) {
        drawScene(*active, true);
    }
    scenes.forEach([&](scene::Scene& scene) {
        if (&scene != active) {
            drawScene(scene, false);
        }
    });
}

void EntityInspector::drawScene(scene::Scene& scene, bool active) {
    const ecs::World& world = scene.world();
    ImGui::PushID(&scene);
    const std::string_view name = scene.name();
    ImGui::SetNextItemOpen(active, ImGuiCond_Once);
    const bool open = ImGui::CollapsingHeader(
        std::format("{}{} ({} entities)###scene", name, active ? " (active)" : "", world.entityCount())
            .c_str());
    if (!open) {
        ImGui::PopID();
        return;
    }

    const std::string_view filter = m_filter.data();
    std::array<char, 32> label{};
    m_shown.clear();
    world.forEachEntity([&](ecs::Entity entity) {
        if (filter.empty()) {
            m_shown.push_back(entity);
            return;
        }
        formatEntity(entity, label);
        bool matches = containsIgnoringCase(label.data(), filter);
        world.forEachComponent(entity, [&](const ecs::ComponentMeta& meta, const void*) {
            matches = matches || containsIgnoringCase(meta.name, filter);
        });
        if (matches) {
            m_shown.push_back(entity);
        }
    });

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(m_shown.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const ecs::Entity entity = m_shown[static_cast<usize>(row)];
            formatEntity(entity, label);
            m_components.clear();
            world.forEachComponent(entity, [&](const ecs::ComponentMeta& meta, const void*) {
                if (!m_components.empty()) {
                    m_components += ", ";
                }
                m_components += meta.name;
            });
            ImGui::PushID(static_cast<int>(entity.index()));
            const bool selected = m_scene == &scene && m_entity == entity;
            if (ImGui::Selectable(label.data(), selected, ImGuiSelectableFlags_AllowOverlap)) {
                select(scene, entity);
            }
            ImGui::SameLine(ImGui::GetFontSize() * 5.0F);
            ImGui::TextDisabled("%s", m_components.c_str());
            ImGui::PopID();
        }
    }
    ImGui::PopID();
}

void EntityInspector::drawInspector(scene::SceneManager& scenes) {
    validate(scenes);
    if (m_scene == nullptr) {
        ImGui::TextDisabled("Select an entity in the Entities window.");
        return;
    }
    const scene::Scene& scene = *m_scene;
    const ecs::World& world = scene.world();
    std::array<char, 32> label{};
    formatEntity(m_entity, label);
    ImGui::Text("Entity %s of scene %.*s", label.data(), static_cast<int>(scene.name().size()),
                scene.name().data());
    ImGui::Separator();

    // Selecting another entity from a field waits until the visit is over.
    const scene::Scene* nextScene = nullptr;
    ecs::Entity next{};
    world.forEachComponent(m_entity, [&](const ecs::ComponentMeta& meta, const void* data) {
        const std::string header(meta.name);
        if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            return;
        }
        if (meta.tag) {
            ImGui::TextDisabled("tag");
            return;
        }
        if (meta.fields.empty()) {
            ImGui::TextDisabled("%u bytes, no described fields", meta.size);
            return;
        }
        if (!ImGui::BeginTable(header.c_str(), 2, ImGuiTableFlags_SizingStretchProp)) {
            return;
        }
        for (const ecs::FieldDescriptor& field : meta.fields) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(field.name.data(), field.name.data() + field.name.size());
            ImGui::TableNextColumn();
            const std::string value = formatField(field, data);
            if (field.type == ecs::FieldType::Color) {
                const auto color = readField<Color>(field, data);
                ImGui::ColorButton("##swatch", ImVec4(color.r, color.g, color.b, color.a),
                                   ImGuiColorEditFlags_NoTooltip,
                                   ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize()));
                ImGui::SameLine();
            }
            if (field.type == ecs::FieldType::Entity) {
                const auto target = readField<ecs::Entity>(field, data);
                if (world.isAlive(target)) {
                    ImGui::PushID(field.name.data());
                    if (ImGui::SmallButton(value.c_str())) {
                        nextScene = &scene;
                        next = target;
                    }
                    ImGui::PopID();
                } else {
                    ImGui::TextDisabled("%s", value.c_str());
                }
                continue;
            }
            ImGui::TextUnformatted(value.c_str());
        }
        ImGui::EndTable();
    });
    if (nextScene != nullptr) {
        select(*nextScene, next);
    }
}

} // namespace teserra::debugui
