#include <teserra/debug_ui/asset_list.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>

namespace teserra::debugui {

namespace {

[[nodiscard]] bool containsIgnoringCase(std::string_view text, std::string_view part) noexcept {
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    return part.empty() || !std::ranges::search(text, part, {}, lower, lower).empty();
}

[[nodiscard]] const char* stateName(assets::AssetState state) noexcept {
    switch (state) {
        case assets::AssetState::Loaded:
            return "loaded";
        case assets::AssetState::Fallback:
            return "fallback";
        case assets::AssetState::Failed:
            break;
    }
    return "failed";
}

} // namespace

void listAssets(const assets::AssetRegistry& registry, std::string_view filter, std::vector<AssetRow>& rows) {
    usize count = 0;
    registry.forEach([&](const assets::AssetInfo& info) {
        if (!containsIgnoringCase(info.path, filter) && !containsIgnoringCase(info.typeName, filter)) {
            return;
        }
        if (count == rows.size()) {
            rows.emplace_back();
        }
        AssetRow& row = rows[count++];
        row.path.assign(info.path); // keeps the string's memory from frame to frame
        row.typeName = info.typeName;
        row.state = info.state;
        row.references = info.references;
    });
    rows.resize(count);
    std::ranges::sort(rows, {}, &AssetRow::path);
}

void AssetList::draw(const assets::AssetRegistry& registry) {
    std::array<u32, 3> counts{};
    registry.forEach([&](const assets::AssetInfo& info) { ++counts[static_cast<usize>(info.state)]; });
    ImGui::Text("%u loaded, %u fallbacks, %u failed", counts[0], counts[1], counts[2]);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##filter", "Filter: path or type", m_filter.data(), m_filter.size());
    listAssets(registry, m_filter.data(), m_rows);

    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                       ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable("##assets", 4, kFlags)) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Type");
    ImGui::TableSetupColumn("State");
    ImGui::TableSetupColumn("Handles");
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(m_rows.size()));
    while (clipper.Step()) {
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
            const AssetRow& row = m_rows[static_cast<usize>(index)];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.path.c_str());
            ImGui::TableNextColumn();
            if (row.typeName.empty()) {
                ImGui::TextDisabled("unnamed");
            } else {
                ImGui::TextUnformatted(row.typeName.data(), row.typeName.data() + row.typeName.size());
            }
            ImGui::TableNextColumn();
            switch (row.state) {
                case assets::AssetState::Loaded:
                    ImGui::TextUnformatted(stateName(row.state));
                    break;
                case assets::AssetState::Fallback:
                    ImGui::TextColored({1.0F, 0.85F, 0.3F, 1.0F}, "%s", stateName(row.state));
                    break;
                case assets::AssetState::Failed:
                    ImGui::TextColored({1.0F, 0.4F, 0.35F, 1.0F}, "%s", stateName(row.state));
                    break;
            }
            ImGui::TableNextColumn();
            ImGui::Text("%u", row.references);
        }
    }
    ImGui::EndTable();
}

} // namespace teserra::debugui
