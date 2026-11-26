#pragma once

// The in-game debug tools' Dear ImGui context (ADR-0013). A frame of the debug UI:
//
//   debugUi.beginFrame(displaySize, framebufferScale, deltaSeconds);
//   ImGui::Begin("Stats"); ImGui::Text("%d entities", count); ImGui::End();   // any ImGui:: calls
//   debugUi.endFrame();
//   imguiPass.setDrawData(debugUi.drawData());   // drawn by the renderer's overlay pass (imgui_pass.hpp)
//
// ImGui is a public dependency of this module only: games include <imgui.h> to write their own windows.

#include <teserra/core/math.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/platform/event.hpp>
#include <teserra/platform/key.hpp>
#include <teserra/rhi/handles.hpp>

#include <imgui.h>

#include <array>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace teserra::debugui {

/// Creation parameters of a DebugUi.
struct DebugUiConfig {
    /// The window's content scale (platform::Window::contentScale()): fonts and sizes grow with it, so the
    /// debug UI reads the same on a 150% display as on a 100% one.
    f32 contentScale = 1.0F;
};

/// A window of the debug UI, listed in its View menu (DebugUi::addWindow()).
struct DebugWindow {
    std::string name;
    /// Makes the window's contents with ImGui:: calls, between its ImGui::Begin() and End().
    std::function<void()> draw;
    bool open = true;
};

/// Owns one Dear ImGui context, set up for Teserra's renderer backend (ImGuiPass): textures are created and
/// updated by the pass as ImGui asks (ImGuiBackendFlags_RendererHasTextures), and large meshes use vertex
/// offsets. Nothing is written to disk (no imgui.ini). Main thread only; the context is made current by the
/// functions that need it, so several DebugUi objects can exist.
class DebugUi {
public:
    [[nodiscard]] static Result<std::unique_ptr<DebugUi>> create(const DebugUiConfig& config = {});
    /// Marks every texture of the context destroyed for ImGui, then destroys the context. The ImGuiPass that
    /// drew it releases the GPU textures itself.
    ~DebugUi();

    DebugUi(const DebugUi&) = delete;
    DebugUi& operator=(const DebugUi&) = delete;
    DebugUi(DebugUi&&) = delete;
    DebugUi& operator=(DebugUi&&) = delete;

    /// Starts a frame for a display of `displaySize` window units, with `framebufferScale` framebuffer pixels
    /// per unit and `deltaSeconds` since the last frame (a small positive value is used for 0). Makes the
    /// context current; ImGui:: calls are valid until endFrame().
    void beginFrame(Vec2 displaySize, Vec2 framebufferScale, f32 deltaSeconds);
    /// Ends the frame and builds its draw data (ImGui::Render()).
    void endFrame();
    /// Whether a frame is between beginFrame() and endFrame().
    [[nodiscard]] bool inFrame() const noexcept { return m_inFrame; }

    /// Feeds a platform event to ImGui (ADR-0013: engine events, not imgui_impl_glfw): keys and modifiers,
    /// mouse buttons, movement and wheel, typed text and focus. Call it for every event of a frame, before
    /// beginFrame(); window sizes come with beginFrame().
    void processEvent(const platform::Event& event);
    /// Whether the debug UI uses the mouse or the keyboard (ImGui's WantCaptureMouse / WantCaptureKeyboard),
    /// as of the last beginFrame(): the mouse is over or dragging one of its windows, or a text field has
    /// focus. The game should not react to that input meanwhile.
    [[nodiscard]] bool wantsMouse() const noexcept;
    [[nodiscard]] bool wantsKeyboard() const noexcept;

    /// What the last endFrame() built, for ImGuiPass::setDrawData(); nullptr before the first frame. Valid
    /// until the next beginFrame().
    [[nodiscard]] ImDrawData* drawData() const noexcept;
    /// The ImGui context, for code that switches contexts itself.
    [[nodiscard]] ImGuiContext* context() const noexcept { return m_context; }

    /// Adds a window titled `name`, which must be unique (asserted), whose contents `draw` makes every frame
    /// the window is open. The View menu lists it and opens or closes it, as does its title bar's close
    /// button.
    void addWindow(std::string name, std::function<void()> draw, bool open = true);
    /// The windows in the order they were added.
    [[nodiscard]] std::span<const DebugWindow> windows() const noexcept { return m_windows; }
    /// Opens or closes the window `name`; false if there is none.
    bool setWindowOpen(std::string_view name, bool open);
    /// Draws the main menu bar with the View menu, then every open window. Inside a frame (asserted).
    void drawWindows();

private:
    explicit DebugUi(ImGuiContext* context) noexcept
        : m_context(context) {}

    ImGuiContext* m_context;
    bool m_inFrame = false;
    // Left and right Ctrl, Shift, Alt and Super held, for ImGui's modifier keys.
    std::array<bool, 8> m_modifiers{};
    std::vector<DebugWindow> m_windows;
};

/// The ImGui key of an engine key; ImGuiKey_None for Key::Unknown and for keys ImGui has none for (World1).
[[nodiscard]] ImGuiKey imguiKey(platform::Key key) noexcept;

/// The ImTextureID of an RHI texture, to show it with ImGui::Image(); the ImGui pass samples it.
[[nodiscard]] ImTextureID textureId(rhi::TextureHandle texture) noexcept;
/// The RHI texture of an ImTextureID made by textureId(), or by the pass for ImGui's own textures.
[[nodiscard]] rhi::TextureHandle textureFromId(ImTextureID id) noexcept;

} // namespace teserra::debugui
