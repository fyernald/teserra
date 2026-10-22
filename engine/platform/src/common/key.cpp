#include <teserra/platform/key.hpp>

#include <teserra/core/assert.hpp>

namespace teserra::platform {

std::string_view keyName(Key key) noexcept {
    switch (key) {
        case Key::Unknown:
            return "Unknown";
        case Key::A:
            return "A";
        case Key::B:
            return "B";
        case Key::C:
            return "C";
        case Key::D:
            return "D";
        case Key::E:
            return "E";
        case Key::F:
            return "F";
        case Key::G:
            return "G";
        case Key::H:
            return "H";
        case Key::I:
            return "I";
        case Key::J:
            return "J";
        case Key::K:
            return "K";
        case Key::L:
            return "L";
        case Key::M:
            return "M";
        case Key::N:
            return "N";
        case Key::O:
            return "O";
        case Key::P:
            return "P";
        case Key::Q:
            return "Q";
        case Key::R:
            return "R";
        case Key::S:
            return "S";
        case Key::T:
            return "T";
        case Key::U:
            return "U";
        case Key::V:
            return "V";
        case Key::W:
            return "W";
        case Key::X:
            return "X";
        case Key::Y:
            return "Y";
        case Key::Z:
            return "Z";
        case Key::Digit0:
            return "0";
        case Key::Digit1:
            return "1";
        case Key::Digit2:
            return "2";
        case Key::Digit3:
            return "3";
        case Key::Digit4:
            return "4";
        case Key::Digit5:
            return "5";
        case Key::Digit6:
            return "6";
        case Key::Digit7:
            return "7";
        case Key::Digit8:
            return "8";
        case Key::Digit9:
            return "9";
        case Key::F1:
            return "F1";
        case Key::F2:
            return "F2";
        case Key::F3:
            return "F3";
        case Key::F4:
            return "F4";
        case Key::F5:
            return "F5";
        case Key::F6:
            return "F6";
        case Key::F7:
            return "F7";
        case Key::F8:
            return "F8";
        case Key::F9:
            return "F9";
        case Key::F10:
            return "F10";
        case Key::F11:
            return "F11";
        case Key::F12:
            return "F12";
        case Key::F13:
            return "F13";
        case Key::F14:
            return "F14";
        case Key::F15:
            return "F15";
        case Key::F16:
            return "F16";
        case Key::F17:
            return "F17";
        case Key::F18:
            return "F18";
        case Key::F19:
            return "F19";
        case Key::F20:
            return "F20";
        case Key::F21:
            return "F21";
        case Key::F22:
            return "F22";
        case Key::F23:
            return "F23";
        case Key::F24:
            return "F24";
        case Key::Escape:
            return "Escape";
        case Key::Enter:
            return "Enter";
        case Key::Tab:
            return "Tab";
        case Key::Backspace:
            return "Backspace";
        case Key::Space:
            return "Space";
        case Key::Insert:
            return "Insert";
        case Key::Delete:
            return "Delete";
        case Key::Home:
            return "Home";
        case Key::End:
            return "End";
        case Key::PageUp:
            return "Page Up";
        case Key::PageDown:
            return "Page Down";
        case Key::Left:
            return "Left";
        case Key::Right:
            return "Right";
        case Key::Up:
            return "Up";
        case Key::Down:
            return "Down";
        case Key::CapsLock:
            return "Caps Lock";
        case Key::ScrollLock:
            return "Scroll Lock";
        case Key::NumLock:
            return "Num Lock";
        case Key::PrintScreen:
            return "Print Screen";
        case Key::Pause:
            return "Pause";
        case Key::Menu:
            return "Menu";
        case Key::Apostrophe:
            return "'";
        case Key::Comma:
            return ",";
        case Key::Minus:
            return "-";
        case Key::Period:
            return ".";
        case Key::Slash:
            return "/";
        case Key::Semicolon:
            return ";";
        case Key::Equal:
            return "=";
        case Key::LeftBracket:
            return "[";
        case Key::Backslash:
            return "\\";
        case Key::RightBracket:
            return "]";
        case Key::GraveAccent:
            return "`";
        case Key::World1:
            return "World 1";
        case Key::World2:
            return "World 2";
        case Key::Numpad0:
            return "Numpad 0";
        case Key::Numpad1:
            return "Numpad 1";
        case Key::Numpad2:
            return "Numpad 2";
        case Key::Numpad3:
            return "Numpad 3";
        case Key::Numpad4:
            return "Numpad 4";
        case Key::Numpad5:
            return "Numpad 5";
        case Key::Numpad6:
            return "Numpad 6";
        case Key::Numpad7:
            return "Numpad 7";
        case Key::Numpad8:
            return "Numpad 8";
        case Key::Numpad9:
            return "Numpad 9";
        case Key::NumpadDecimal:
            return "Numpad .";
        case Key::NumpadDivide:
            return "Numpad /";
        case Key::NumpadMultiply:
            return "Numpad *";
        case Key::NumpadSubtract:
            return "Numpad -";
        case Key::NumpadAdd:
            return "Numpad +";
        case Key::NumpadEnter:
            return "Numpad Enter";
        case Key::NumpadEqual:
            return "Numpad =";
        case Key::LeftShift:
            return "Left Shift";
        case Key::LeftControl:
            return "Left Control";
        case Key::LeftAlt:
            return "Left Alt";
        case Key::LeftSuper:
            return "Left Super";
        case Key::RightShift:
            return "Right Shift";
        case Key::RightControl:
            return "Right Control";
        case Key::RightAlt:
            return "Right Alt";
        case Key::RightSuper:
            return "Right Super";
    }
    TESERRA_UNREACHABLE("Invalid Key value {}", static_cast<usize>(key));
}

} // namespace teserra::platform
