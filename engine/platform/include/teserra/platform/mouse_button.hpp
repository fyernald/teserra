#pragma once

#include <teserra/core/types.hpp>

#include <string_view>

namespace teserra::platform {

/// A mouse button. Back and Forward are the side buttons that browsers use for navigation; mice with more
/// buttons report only these five.
enum class MouseButton : u8 {
    Left,
    Right,
    Middle,
    Back,
    Forward,
};

/// Number of MouseButton values; Forward must stay the last enumerator.
inline constexpr usize kMouseButtonCount = static_cast<usize>(MouseButton::Forward) + 1;

/// English name of `button` for logs and debug UI ("Left", "Back").
[[nodiscard]] std::string_view mouseButtonName(MouseButton button) noexcept;

} // namespace teserra::platform
