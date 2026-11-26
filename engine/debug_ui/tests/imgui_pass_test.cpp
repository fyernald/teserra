// The ImGui pass on the null backend: what it records for a frame of a DebugUi, drawn as an overlay of the
// renderer after the composite pass. opengl tests and the snapshot test check the pixels.

#include <teserra/debug_ui/imgui_pass.hpp>

#include <teserra/debug_ui/debug_ui.hpp>
#include <teserra/renderer/renderer.hpp>
#include <teserra/rhi/command_list.hpp>
#include <teserra/rhi_null/null_device.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace teserra;
using namespace teserra::debugui;

namespace {

constexpr UVec2 kOutputSize{320, 200};

// A device, a renderer with the ImGui pass as its overlay, a DebugUi and an output texture.
struct Fixture {
    Fixture() {
        auto created = ImGuiPass::create(*device);
        REQUIRE(created.has_value());
        pass = created->get();
        renderer->addOverlayPass(std::move(created).value());
        output = *device->createTexture({.format = rhi::Format::Rgba8Srgb,
                                         .width = kOutputSize.x,
                                         .height = kOutputSize.y,
                                         .usage = rhi::TextureUsage::RenderTarget});
    }

    // Runs one frame of the debug UI with `windows` (ImGui:: calls), renders it, and returns the commands of
    // the overlay, from its debug group on.
    template <typename Windows>
    std::string frame(Windows&& windows) {
        debugUi->beginFrame(Vec2{kOutputSize}, Vec2{1.0F}, 1.0F / 60.0F);
        windows();
        debugUi->endFrame();
        pass->setDrawData(debugUi->drawData());

        device->beginFrame();
        rhi::CommandList& commands = device->acquireCommandList();
        renderer->render({}, {.texture = output, .format = rhi::Format::Rgba8Srgb, .size = kOutputSize},
                         commands);
        device->submit(commands);
        const std::string described = rhi::describeCommands(*device->submitted()[0]);
        device->endFrame();
        const usize overlay = described.find("PushDebugGroup \"Debug UI\"");
        return overlay == std::string::npos ? std::string{} : described.substr(overlay);
    }

    std::unique_ptr<rhi::null::NullDevice> device =
        rhi::null::createHeadlessDevice({.enableValidation = true});
    std::unique_ptr<renderer::Renderer> renderer = *renderer::Renderer::create(*device);
    std::unique_ptr<DebugUi> debugUi = *DebugUi::create();
    ImGuiPass* pass = nullptr;
    rhi::TextureHandle output;
};

std::vector<std::string> linesStartingWith(const std::string& text, std::string_view prefix) {
    std::vector<std::string> found;
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        line = line.substr(std::min(line.find_first_not_of(' '), line.size()));
        if (line.starts_with(prefix)) {
            found.push_back(line);
        }
    }
    return found;
}

void testWindow() {
    ImGui::SetNextWindowPos({10.0F, 20.0F});
    ImGui::SetNextWindowSize({200.0F, 100.0F});
    ImGui::Begin("Test");
    ImGui::Text("Hello from the debug UI");
    ImGui::End();
}

} // namespace

TEST_CASE("ImGuiPass: draws a window after the composite, one draw per command with its scissor",
          "[debug_ui][imgui]") {
    Fixture fixture;

    const std::string commands = fixture.frame(testWindow);

    REQUIRE_FALSE(commands.empty());
    CHECK(commands.find("BeginRenderPass") != std::string::npos);
    const std::vector<std::string> draws = linesStartingWith(commands, "DrawIndexed");
    const std::vector<std::string> scissors = linesStartingWith(commands, "SetScissor");
    CHECK_FALSE(draws.empty());
    CHECK(draws.size() == scissors.size());
    CHECK(fixture.pass->stats().drawCalls == draws.size());
    CHECK(fixture.pass->stats().vertices > 0);
    // The font atlas: created for ImGui on the first frame, then kept.
    CHECK(fixture.pass->stats().textures == 1);
    const u32 textures = fixture.pass->stats().textures;
    static_cast<void>(fixture.frame(testWindow));
    CHECK(fixture.pass->stats().textures == textures);
}

TEST_CASE("ImGuiPass: a frame without windows records nothing", "[debug_ui][imgui]") {
    Fixture fixture;

    const std::string commands = fixture.frame([] {});

    CHECK(linesStartingWith(commands, "DrawIndexed").empty());
    CHECK(fixture.pass->stats().drawCalls == 0);
}

TEST_CASE("ImGuiPass: without draw data it draws nothing", "[debug_ui][imgui]") {
    Fixture fixture;
    fixture.device->beginFrame();
    rhi::CommandList& commands = fixture.device->acquireCommandList();
    fixture.renderer->render(
        {}, {.texture = fixture.output, .format = rhi::Format::Rgba8Srgb, .size = kOutputSize}, commands);
    fixture.device->submit(commands);
    fixture.device->endFrame();

    CHECK(fixture.pass->stats().drawCalls == 0);
}

TEST_CASE("textureId: round-trips RHI texture handles, and the null handle is ImGui's invalid id",
          "[debug_ui]") {
    const rhi::TextureHandle texture{7, 3};
    CHECK(textureFromId(textureId(texture)) == texture);
    CHECK(textureId(rhi::TextureHandle{}) == ImTextureID_Invalid);
}
