// Input for the debug UI (ADR-0013): engine platform events into ImGui's IO, never imgui_impl_glfw.

#include <teserra/debug_ui/debug_ui.hpp>
#include <teserra/platform/event.hpp>
#include <teserra/platform/key.hpp>
#include <teserra/platform/mouse_button.hpp>

#include <variant>

namespace teserra::debugui {

ImGuiKey imguiKey(platform::Key key) noexcept {
    using platform::Key;
    const auto offset = [](Key value, Key first, ImGuiKey firstImGui) {
        return static_cast<ImGuiKey>(static_cast<int>(firstImGui) +
                                     (static_cast<int>(value) - static_cast<int>(first)));
    };
    if (key >= Key::A && key <= Key::Z) {
        return offset(key, Key::A, ImGuiKey_A);
    }
    if (key >= Key::Digit0 && key <= Key::Digit9) {
        return offset(key, Key::Digit0, ImGuiKey_0);
    }
    if (key >= Key::F1 && key <= Key::F24) {
        return offset(key, Key::F1, ImGuiKey_F1);
    }
    if (key >= Key::Numpad0 && key <= Key::Numpad9) {
        return offset(key, Key::Numpad0, ImGuiKey_Keypad0);
    }
    switch (key) {
        case Key::Escape:
            return ImGuiKey_Escape;
        case Key::Enter:
            return ImGuiKey_Enter;
        case Key::Tab:
            return ImGuiKey_Tab;
        case Key::Backspace:
            return ImGuiKey_Backspace;
        case Key::Space:
            return ImGuiKey_Space;
        case Key::Insert:
            return ImGuiKey_Insert;
        case Key::Delete:
            return ImGuiKey_Delete;
        case Key::Home:
            return ImGuiKey_Home;
        case Key::End:
            return ImGuiKey_End;
        case Key::PageUp:
            return ImGuiKey_PageUp;
        case Key::PageDown:
            return ImGuiKey_PageDown;
        case Key::Left:
            return ImGuiKey_LeftArrow;
        case Key::Right:
            return ImGuiKey_RightArrow;
        case Key::Up:
            return ImGuiKey_UpArrow;
        case Key::Down:
            return ImGuiKey_DownArrow;
        case Key::CapsLock:
            return ImGuiKey_CapsLock;
        case Key::ScrollLock:
            return ImGuiKey_ScrollLock;
        case Key::NumLock:
            return ImGuiKey_NumLock;
        case Key::PrintScreen:
            return ImGuiKey_PrintScreen;
        case Key::Pause:
            return ImGuiKey_Pause;
        case Key::Menu:
            return ImGuiKey_Menu;
        case Key::Apostrophe:
            return ImGuiKey_Apostrophe;
        case Key::Comma:
            return ImGuiKey_Comma;
        case Key::Minus:
            return ImGuiKey_Minus;
        case Key::Period:
            return ImGuiKey_Period;
        case Key::Slash:
            return ImGuiKey_Slash;
        case Key::Semicolon:
            return ImGuiKey_Semicolon;
        case Key::Equal:
            return ImGuiKey_Equal;
        case Key::LeftBracket:
            return ImGuiKey_LeftBracket;
        case Key::Backslash:
            return ImGuiKey_Backslash;
        case Key::RightBracket:
            return ImGuiKey_RightBracket;
        case Key::GraveAccent:
            return ImGuiKey_GraveAccent;
        case Key::World2:
            return ImGuiKey_Oem102; // the key between left Shift and Z on ISO keyboards
        case Key::NumpadDecimal:
            return ImGuiKey_KeypadDecimal;
        case Key::NumpadDivide:
            return ImGuiKey_KeypadDivide;
        case Key::NumpadMultiply:
            return ImGuiKey_KeypadMultiply;
        case Key::NumpadSubtract:
            return ImGuiKey_KeypadSubtract;
        case Key::NumpadAdd:
            return ImGuiKey_KeypadAdd;
        case Key::NumpadEnter:
            return ImGuiKey_KeypadEnter;
        case Key::NumpadEqual:
            return ImGuiKey_KeypadEqual;
        case Key::LeftShift:
            return ImGuiKey_LeftShift;
        case Key::LeftControl:
            return ImGuiKey_LeftCtrl;
        case Key::LeftAlt:
            return ImGuiKey_LeftAlt;
        case Key::LeftSuper:
            return ImGuiKey_LeftSuper;
        case Key::RightShift:
            return ImGuiKey_RightShift;
        case Key::RightControl:
            return ImGuiKey_RightCtrl;
        case Key::RightAlt:
            return ImGuiKey_RightAlt;
        case Key::RightSuper:
            return ImGuiKey_RightSuper;
        default:
            return ImGuiKey_None; // Unknown, and World1, which ImGui has no key for
    }
}

void DebugUi::processEvent(const platform::Event& event) {
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_context);
    ImGuiIO& io = ImGui::GetIO();
    if (const auto* key = std::get_if<platform::KeyChanged>(&event)) {
        // Modifiers are ImGui keys of their own, from both sides of the keyboard.
        const auto modifier = [&](platform::Key left, platform::Key right, bool& leftDown, bool& rightDown,
                                  ImGuiKey mod) {
            if (key->key != left && key->key != right) {
                return;
            }
            (key->key == left ? leftDown : rightDown) = key->down;
            io.AddKeyEvent(mod, leftDown || rightDown);
        };
        modifier(platform::Key::LeftControl, platform::Key::RightControl, m_modifiers[0], m_modifiers[1],
                 ImGuiMod_Ctrl);
        modifier(platform::Key::LeftShift, platform::Key::RightShift, m_modifiers[2], m_modifiers[3],
                 ImGuiMod_Shift);
        modifier(platform::Key::LeftAlt, platform::Key::RightAlt, m_modifiers[4], m_modifiers[5],
                 ImGuiMod_Alt);
        modifier(platform::Key::LeftSuper, platform::Key::RightSuper, m_modifiers[6], m_modifiers[7],
                 ImGuiMod_Super);
        if (const ImGuiKey imgui = imguiKey(key->key); imgui != ImGuiKey_None) {
            io.AddKeyEvent(imgui, key->down);
        }
    } else if (const auto* button = std::get_if<platform::MouseButtonChanged>(&event)) {
        // Left, right, middle, back and forward are ImGui's mouse buttons 0 to 4, in that order.
        io.AddMouseButtonEvent(static_cast<int>(button->button), button->down);
    } else if (const auto* moved = std::get_if<platform::MouseMoved>(&event)) {
        io.AddMousePosEvent(moved->position.x, moved->position.y);
    } else if (const auto* scrolled = std::get_if<platform::MouseScrolled>(&event)) {
        io.AddMouseWheelEvent(scrolled->offset.x, scrolled->offset.y);
    } else if (const auto* text = std::get_if<platform::TextEntered>(&event)) {
        io.AddInputCharacter(static_cast<unsigned int>(text->codepoint));
    } else if (const auto* focus = std::get_if<platform::FocusChanged>(&event)) {
        io.AddFocusEvent(focus->focused);
        if (!focus->focused) {
            m_modifiers.fill(false); // the platform reports held keys as released; nothing stays held
        }
    }
    ImGui::SetCurrentContext(previous);
}

bool DebugUi::wantsMouse() const noexcept {
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_context);
    const bool wants = ImGui::GetIO().WantCaptureMouse;
    ImGui::SetCurrentContext(previous);
    return wants;
}

bool DebugUi::wantsKeyboard() const noexcept {
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_context);
    const bool wants = ImGui::GetIO().WantCaptureKeyboard;
    ImGui::SetCurrentContext(previous);
    return wants;
}

} // namespace teserra::debugui
