#pragma once

// The debug UI's asset list (#301): the assets of an AssetRegistry, their type, their state and their
// handles, with the failed loads highlighted.
//
//   AssetList list;
//   debugUi.addWindow("Assets", [&] { list.draw(app.assets()); });

#include <teserra/assets/asset_registry.hpp>
#include <teserra/core/types.hpp>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace teserra::debugui {

/// A row of the asset list.
struct AssetRow {
    std::string path;
    std::string_view typeName; // static: a loader's type name
    assets::AssetState state = assets::AssetState::Loaded;
    u32 references = 0;
};

/// The rows of `registry`'s assets whose path or type name contains `filter` (ignoring case), sorted by path,
/// into `rows` (whose memory is reused).
void listAssets(const assets::AssetRegistry& registry, std::string_view filter, std::vector<AssetRow>& rows);

/// The asset window's state: its filter and its rows.
class AssetList {
public:
    /// Draws into the current ImGui window the counts by state, a filter and a table of the assets: path,
    /// type, state (fallbacks in yellow, failures in red) and handles.
    void draw(const assets::AssetRegistry& registry);

private:
    std::array<char, 64> m_filter{};
    std::vector<AssetRow> m_rows;
};

} // namespace teserra::debugui
