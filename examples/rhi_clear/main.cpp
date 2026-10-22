// RHI clear: the first output through the RHI (#28). Every frame records one render pass that clears the
// swapchain image to a color cycling over time, and presents it. Only RHI calls: no backend appears here.
//
//   teserra_rhi_clear                              runs until Escape or close
//   teserra_rhi_clear --frames 60 --hidden-window  smoke run: 60 frames in a hidden window

#include <teserra/core/log.hpp>
#include <teserra/core/math.hpp>
#include <teserra/engine/app.hpp>
#include <teserra/platform/key.hpp>
#include <teserra/rhi/command_list.hpp>
#include <teserra/rhi/render_pass.hpp>

#include <cmath>
#include <numbers>

using namespace teserra;

namespace {

// A color on a slow loop around the hue circle, in linear values: the sRGB swapchain encodes it.
Vec4 clearColor(f32 seconds) {
    constexpr f32 kTwoPi = 2.0F * std::numbers::pi_v<f32>;
    const f32 phase = seconds * 0.25F * kTwoPi;
    const auto channel = [&](f32 offset) { return 0.5F + (0.5F * std::sin(phase + (offset * kTwoPi))); };
    return {channel(0.0F), channel(1.0F / 3.0F), channel(2.0F / 3.0F), 1.0F};
}

} // namespace

int main(int argc, char** argv) { // NOLINT(bugprone-exception-escape)
    engine::App app{{.title = "Teserra: RHI clear", .args = {argc, argv}}};
    if (!app.isInitialized()) {
        return 1;
    }
    TESERRA_LOG_INFO(LogCategory::App, "Rendering with the {} backend; press Escape to quit",
                     rhi::backendName(app.device().backend()));

    f32 seconds = 0.0F;
    return app.run({
        .update =
            [&](engine::App& running, f32 deltaSeconds) {
                seconds += deltaSeconds;
                if (running.input().wasPressed(platform::Key::Escape)) {
                    running.quit();
                }
            },
        .render =
            [&](engine::App& running, rhi::CommandList& commands) {
                commands.pushDebugGroup("Clear");
                commands.beginRenderPass({.colorAttachments = {{
                                              .texture = running.backbuffer(),
                                              .loadOp = rhi::LoadOp::Clear,
                                              .storeOp = rhi::StoreOp::Store,
                                              .clearColor = clearColor(seconds),
                                          }}});
                commands.endRenderPass();
                commands.popDebugGroup();
            },
    });
}
