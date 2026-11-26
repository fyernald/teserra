#include <teserra/debug_ui/debug_ui.hpp>

#include <teserra/core/assert.hpp>

#include <algorithm>

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

ImTextureID textureId(rhi::TextureHandle texture) noexcept {
    return (static_cast<ImTextureID>(texture.generation()) << 32U) | texture.index();
}

rhi::TextureHandle textureFromId(ImTextureID id) noexcept {
    return {static_cast<u32>(id & 0xFFFF'FFFFU), static_cast<u32>(id >> 32U)};
}

} // namespace teserra::debugui
