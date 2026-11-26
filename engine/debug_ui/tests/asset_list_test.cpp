// The asset list (#301): rows sorted by path, filtered by path or type name ignoring case, and the window
// draws.

#include <teserra/debug_ui/asset_list.hpp>

#include <teserra/assets/asset_registry.hpp>
#include <teserra/core/result.hpp>
#include <teserra/debug_ui/debug_ui.hpp>
#include <teserra/test/log_capture.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace teserra;
using namespace teserra::debugui;

namespace {

struct Blob {
    int value = 0;
};

Result<Blob> blob() {
    return Blob{};
}

Result<Blob> missing() {
    return fail(ErrorCode::NotFound, Module::Game, "missing");
}

std::vector<std::string> paths(const std::vector<AssetRow>& rows) {
    std::vector<std::string> found;
    for (const AssetRow& row : rows) {
        found.push_back(row.path);
    }
    return found;
}

} // namespace

TEST_CASE("listAssets: rows sorted by path and filtered by path, ignoring case", "[debug_ui][assets]") {
    const test::LogCapture logs;
    assets::AssetRegistry registry;
    const auto hero = registry.load<Blob>("game://sprites/Hero.png", blob);
    const auto atlas = registry.load<Blob>("game://atlas.png", blob);
    const auto broken = registry.load<Blob>("game://broken.png", missing);

    std::vector<AssetRow> rows;
    listAssets(registry, {}, rows);
    CHECK(paths(rows) ==
          std::vector<std::string>{"game://atlas.png", "game://broken.png", "game://sprites/Hero.png"});
    CHECK(rows[1].state == assets::AssetState::Failed);

    listAssets(registry, "HERO", rows);
    CHECK(paths(rows) == std::vector<std::string>{"game://sprites/Hero.png"});
    CHECK(rows[0].references == 1);
}

TEST_CASE("AssetList: draws the assets of a registry", "[debug_ui][assets]") {
    const std::unique_ptr<DebugUi> debugUi = *DebugUi::create();
    assets::AssetRegistry registry;
    const auto atlas = registry.load<Blob>("game://atlas.png", blob);
    AssetList list;
    debugUi->addWindow("Assets", [&] { list.draw(registry); });
    for (int frame = 0; frame < 2; ++frame) {
        debugUi->beginFrame({800.0F, 600.0F}, Vec2{1.0F}, 1.0F / 60.0F);
        debugUi->drawWindows();
        debugUi->endFrame();
    }
    CHECK(debugUi->drawData()->TotalVtxCount > 0);
}
