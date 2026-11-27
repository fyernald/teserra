#include <teserra/debug_ui/debug_ui.hpp>

#include <teserra/core/assert.hpp>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace teserra::debugui {

Result<std::unique_ptr<DebugUi>> DebugUi::create(const DebugUiConfig& config) {
    IMGUI_CHECKVERSION();
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    if (context == nullptr) {
        return fail(ErrorCode::OutOfMemory, Module::App, "Cannot create the Dear ImGui context");
    }
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // nothing written to disk
    io.LogFilename = nullptr;
    io.BackendRendererName = "teserra_rhi";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();
    ImGui::StyleColorsDark();
    const f32 scale = std::max(config.contentScale, 0.5F);
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetStyle().FontScaleDpi = scale;
    ImGui::SetCurrentContext(previous);
    // The constructor is private, so make_unique cannot reach it.
    return std::unique_ptr<DebugUi>(new DebugUi(context)); // NOLINT(cppcoreguidelines-owning-memory)
}

DebugUi::~DebugUi() {
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_context);
    // The ImGuiPass that drew the textures destroys their GPU objects; ImGui only needs to know they are
    // gone.
    for (ImTextureData* texture : ImGui::GetPlatformIO().Textures) {
        if (texture->Status != ImTextureStatus_Destroyed) {
            texture->SetTexID(ImTextureID_Invalid);
            texture->SetStatus(ImTextureStatus_Destroyed);
        }
    }
    ImGui::DestroyContext(m_context);
    ImGui::SetCurrentContext(previous == m_context ? nullptr : previous);
}

void DebugUi::beginFrame(Vec2 displaySize, Vec2 framebufferScale, f32 deltaSeconds) {
    TESERRA_ASSERT(!m_inFrame, "DebugUi::beginFrame() twice without endFrame()");
    ImGui::SetCurrentContext(m_context);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {std::max(displaySize.x, 0.0F), std::max(displaySize.y, 0.0F)};
    io.DisplayFramebufferScale = {framebufferScale.x, framebufferScale.y};
    io.DeltaTime = deltaSeconds > 0.0F ? deltaSeconds : 1.0F / 60.0F;
    ImGui::NewFrame();
    m_inFrame = true;
}

void DebugUi::endFrame() {
    TESERRA_ASSERT(m_inFrame, "DebugUi::endFrame() without beginFrame()");
    ImGui::SetCurrentContext(m_context);
    ImGui::Render();
    m_inFrame = false;
}

ImDrawData* DebugUi::drawData() const noexcept {
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_context);
    ImDrawData* data = ImGui::GetDrawData();
    ImGui::SetCurrentContext(previous);
    return data;
}

void DebugUi::addWindow(std::string name, std::function<void()> draw, bool open) {
    TESERRA_ASSERT(
        std::ranges::none_of(m_windows, [&](const DebugWindow& window) { return window.name == name; }),
        "The debug UI already has a window named '{}'", name);
    m_windows.push_back({.name = std::move(name), .draw = std::move(draw), .open = open});
    m_placements.emplace_back();
}

bool DebugUi::setWindowOpen(std::string_view name, bool open) {
    const auto window = std::ranges::find(m_windows, name, &DebugWindow::name);
    if (window == m_windows.end()) {
        return false;
    }
    window->open = open;
    return true;
}

void DebugUi::drawWindows() {
    TESERRA_ASSERT(m_inFrame, "DebugUi::drawWindows() outside a frame");
    f32 menuBarHeight = 0.0F;
    if (ImGui::BeginMainMenuBar()) {
        menuBarHeight = ImGui::GetWindowSize().y;
        if (ImGui::BeginMenu("View")) {
            for (DebugWindow& window : m_windows) {
                ImGui::MenuItem(window.name.c_str(), nullptr, &window.open);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    // A window first shows at its own free place below the menu bar (#308). ImGui sizes a new window to its
    // contents on its first frame, which it does not show, so the window is placed on its second: in the
    // first free spot, top to bottom and left to right, where its size fits next to the other open windows.
    // Once placed, a window stays where it is, or where the user moves it. The viewport's work area only
    // leaves out the menu bar from the next frame on, so the bar's height is used.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const Vec2 area{viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y};
    const f32 gap = ImGui::GetStyle().WindowPadding.x;
    const Vec2 origin{viewport->Pos.x + gap, viewport->Pos.y + menuBarHeight + gap};
    for (std::size_t index = 0; index < m_windows.size(); ++index) {
        DebugWindow& window = m_windows[index];
        WindowPlacement& placement = m_placements[index];
        if (!window.open) {
            continue;
        }
        if (!placement.placed) {
            ImGui::SetNextWindowPos({origin.x, origin.y}, ImGuiCond_FirstUseEver);
        }
        // ImGui::End() goes with every Begin(), also when the window is collapsed and draws nothing.
        const bool expanded = ImGui::Begin(window.name.c_str(), &window.open);
        if (!placement.placed && !ImGui::IsWindowAppearing()) {
            const ImVec2 size = ImGui::GetWindowSize();
            const Vec2 spot = freeSpot(index, {size.x, size.y}, origin, area, gap);
            ImGui::SetWindowPos({spot.x, spot.y});
            placement.placed = true;
        }
        const ImVec2 position = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        placement.min = {position.x, position.y};
        placement.max = {position.x + size.x, position.y + size.y};
        if (expanded && window.draw) {
            window.draw();
        }
        ImGui::End();
    }
}

Vec2 DebugUi::freeSpot(std::size_t index, Vec2 size, Vec2 origin, Vec2 area, f32 gap) const {
    // The other open windows that have a place; those after `index` have last frame's.
    const auto isObstacle = [&](std::size_t other) {
        return other != index && m_windows[other].open && m_placements[other].placed;
    };
    const auto isFree = [&](Vec2 spot) {
        if (spot.x + size.x > area.x || spot.y + size.y > area.y) {
            return false;
        }
        for (std::size_t other = 0; other < m_windows.size(); ++other) {
            const WindowPlacement& placed = m_placements[other];
            if (isObstacle(other) && spot.x < placed.max.x + gap && placed.min.x < spot.x + size.x + gap &&
                spot.y < placed.max.y + gap && placed.min.y < spot.y + size.y + gap) {
                return false;
            }
        }
        return true;
    };
    // Candidates: the origin, and right of and below every other window.
    std::vector<Vec2> spots{origin};
    for (std::size_t other = 0; other < m_windows.size(); ++other) {
        if (isObstacle(other)) {
            const WindowPlacement& placed = m_placements[other];
            spots.push_back({placed.max.x + gap, std::max(placed.min.y, origin.y)});
            spots.push_back({std::max(placed.min.x, origin.x), placed.max.y + gap});
        }
    }
    std::ranges::sort(spots, [](Vec2 a, Vec2 b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
    const auto spot = std::ranges::find_if(spots, isFree);
    // No room left: the origin, on top of other windows.
    return spot != spots.end() ? *spot : origin;
}

ImTextureID textureId(rhi::TextureHandle texture) noexcept {
    return (static_cast<ImTextureID>(texture.generation()) << 32U) | texture.index();
}

rhi::TextureHandle textureFromId(ImTextureID id) noexcept {
    return {static_cast<u32>(id & 0xFFFF'FFFFU), static_cast<u32>(id >> 32U)};
}

} // namespace teserra::debugui
