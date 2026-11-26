// The debug UI snapshot (#296): a small Dear ImGui window drawn by the ImGui pass as an overlay of the
// renderer, over an empty scene. Built with TESERRA_WITH_DEBUG_UI only. Font edges are filtered, so software
// and hardware rasterizers get a little more room than the default.

#include <teserra/debug_ui/debug_ui.hpp>
#include <teserra/debug_ui/imgui_pass.hpp>
#include <teserra/renderer/frame_packet.hpp>
#include <teserra/renderer/renderer.hpp>

#include "snapshot.hpp"
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>

using namespace teserra;
using namespace teserra::snapshot;

TEST_CASE("Snapshot: debug UI window", "[snapshot][gpu][debug_ui]") {
    SnapshotRenderer snapshot;
    renderer::Renderer& renderer = *snapshot.app().renderer();
    auto created = debugui::ImGuiPass::create(snapshot.device());
    REQUIRE(created.has_value());
    debugui::ImGuiPass& pass = **created;
    renderer.addOverlayPass(std::move(created).value());
    const std::unique_ptr<debugui::DebugUi> debugUi = *debugui::DebugUi::create();

    debugUi->beginFrame(Vec2{kSnapshotSize}, Vec2{1.0F}, 1.0F / 60.0F);
    ImGui::SetNextWindowPos({8.0F, 8.0F});
    ImGui::SetNextWindowSize({144.0F, 74.0F});
    ImGui::Begin("Teserra");
    ImGui::Text("Debug UI");
    ImGui::Button("Button");
    ImGui::End();
    debugUi->endFrame();
    pass.setDrawData(debugUi->drawData());

    renderer::FramePacket packet;
    const Image image = snapshot.render(
        [&](rhi::CommandList& commands) { renderer.render(packet, snapshot.output(), commands); });
    checkSnapshot("debug_ui", image, {.channel = 8, .maxDifferingRatio = 0.03});
}
