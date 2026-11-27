#include <teserra/platform/input.hpp>

#include <teserra/core/assert.hpp>

#include <algorithm>
#include <variant>

namespace teserra::platform {

namespace {

usize indexOf(Key key) {
    const auto index = static_cast<usize>(key);
    TESERRA_ASSERT(index < kKeyCount, "Invalid Key value {}", index);
    return index;
}

usize indexOf(MouseButton button) {
    const auto index = static_cast<usize>(button);
    TESERRA_ASSERT(index < kMouseButtonCount, "Invalid MouseButton value {}", index);
    return index;
}

usize indexOf(GamepadButton button) {
    const auto index = static_cast<usize>(button);
    TESERRA_ASSERT(index < kGamepadButtonCount, "Invalid GamepadButton value {}", index);
    return index;
}

usize indexOf(GamepadAxis axis) {
    const auto index = static_cast<usize>(axis);
    TESERRA_ASSERT(index < kGamepadAxisCount, "Invalid GamepadAxis value {}", index);
    return index;
}

f32 keyAxis(bool negative, bool positive) noexcept {
    return (positive ? 1.0F : 0.0F) - (negative ? 1.0F : 0.0F);
}

// 0 up to `deadzone`, then the rest of the range scaled to 0..1.
f32 rescale(f32 value, f32 deadzone) noexcept {
    return value <= deadzone ? 0.0F : std::min((value - deadzone) / (1.0F - deadzone), 1.0F);
}

} // namespace

void Input::beginFrame() noexcept {
    m_keysPressed.reset();
    m_keysReleased.reset();
    m_buttonsPressed.reset();
    m_buttonsReleased.reset();
    m_mouseDelta = Vec2{0.0F};
    m_scrollDelta = Vec2{0.0F};
    m_text.clear();
    for (Gamepad& gamepad : m_gamepads) {
        gamepad.pressed.reset();
        gamepad.released.reset();
    }
}

void Input::processEvent(const Event& event) {
    if (const auto* key = std::get_if<KeyChanged>(&event)) {
        setKey(key->key, key->down);
    } else if (const auto* button = std::get_if<MouseButtonChanged>(&event)) {
        setButton(button->button, button->down);
    } else if (const auto* moved = std::get_if<MouseMoved>(&event)) {
        moveMouse(moved->position);
    } else if (const auto* scrolled = std::get_if<MouseScrolled>(&event)) {
        m_scrollDelta += scrolled->offset;
    } else if (const auto* text = std::get_if<TextEntered>(&event)) {
        appendText(text->codepoint);
    } else if (const auto* focus = std::get_if<FocusChanged>(&event)) {
        if (!focus->focused) {
            releaseAll();
        }
    } else if (const auto* connection = std::get_if<GamepadConnectionChanged>(&event)) {
        setGamepadConnected(connection->gamepad, connection->connected);
    } else if (const auto* gamepadButton = std::get_if<GamepadButtonChanged>(&event)) {
        setGamepadButton(gamepadButton->gamepad, gamepadButton->button, gamepadButton->down);
    } else if (const auto* axisMoved = std::get_if<GamepadAxisMoved>(&event)) {
        gamepadAt(axisMoved->gamepad).axes[indexOf(axisMoved->axis)] = axisMoved->value;
    }
}

bool Input::isDown(Key key) const {
    return m_keysDown.test(indexOf(key));
}

bool Input::wasPressed(Key key) const {
    return m_keysPressed.test(indexOf(key));
}

bool Input::wasReleased(Key key) const {
    return m_keysReleased.test(indexOf(key));
}

bool Input::isDown(MouseButton button) const {
    return m_buttonsDown.test(indexOf(button));
}

bool Input::wasPressed(MouseButton button) const {
    return m_buttonsPressed.test(indexOf(button));
}

bool Input::wasReleased(MouseButton button) const {
    return m_buttonsReleased.test(indexOf(button));
}

Vec2 Input::axis2D(Key left, Key right, Key down, Key up) const {
    const Vec2 direction{keyAxis(isDown(left), isDown(right)), keyAxis(isDown(down), isDown(up))};
    // Both axes non-zero is the only case longer than 1.
    return direction.x != 0.0F && direction.y != 0.0F ? math::normalize(direction) : direction;
}

bool Input::isConnected(u32 gamepad) const {
    return gamepadAt(gamepad).connected;
}

std::optional<u32> Input::firstGamepad() const noexcept {
    for (u32 index = 0; index < kMaxGamepads; ++index) {
        if (m_gamepads[index].connected) {
            return index;
        }
    }
    return std::nullopt;
}

bool Input::isDown(u32 gamepad, GamepadButton button) const {
    return gamepadAt(gamepad).down.test(indexOf(button));
}

bool Input::wasPressed(u32 gamepad, GamepadButton button) const {
    return gamepadAt(gamepad).pressed.test(indexOf(button));
}

bool Input::wasReleased(u32 gamepad, GamepadButton button) const {
    return gamepadAt(gamepad).released.test(indexOf(button));
}

Vec2 Input::stick(u32 gamepad, GamepadStick side) const {
    const bool left = side == GamepadStick::Left;
    const Vec2 raw{rawAxis(gamepad, left ? GamepadAxis::LeftX : GamepadAxis::RightX),
                   rawAxis(gamepad, left ? GamepadAxis::LeftY : GamepadAxis::RightY)};
    const f32 length = math::length(raw);
    if (length <= m_deadzones.stick) {
        return Vec2{0.0F};
    }
    return raw * (rescale(length, m_deadzones.stick) / length);
}

f32 Input::axis(u32 gamepad, GamepadAxis which) const {
    switch (which) {
        case GamepadAxis::LeftX:
            return stick(gamepad, GamepadStick::Left).x;
        case GamepadAxis::LeftY:
            return stick(gamepad, GamepadStick::Left).y;
        case GamepadAxis::RightX:
            return stick(gamepad, GamepadStick::Right).x;
        case GamepadAxis::RightY:
            return stick(gamepad, GamepadStick::Right).y;
        case GamepadAxis::LeftTrigger:
        case GamepadAxis::RightTrigger:
            return rescale(rawAxis(gamepad, which), m_deadzones.trigger);
    }
    TESERRA_UNREACHABLE("Invalid GamepadAxis value {}", static_cast<usize>(which));
}

f32 Input::rawAxis(u32 gamepad, GamepadAxis axis) const {
    return gamepadAt(gamepad).axes[indexOf(axis)];
}

void Input::setGamepadDeadzones(const GamepadDeadzones& deadzones) {
    TESERRA_ASSERT(deadzones.stick >= 0.0F && deadzones.stick < 1.0F, "A stick deadzone is in [0, 1), not {}",
                   deadzones.stick);
    TESERRA_ASSERT(deadzones.trigger >= 0.0F && deadzones.trigger < 1.0F,
                   "A trigger deadzone is in [0, 1), not {}", deadzones.trigger);
    m_deadzones = deadzones;
}

const Input::Gamepad& Input::gamepadAt(u32 gamepad) const {
    TESERRA_ASSERT(gamepad < kMaxGamepads, "Gamepad index {} is not below kMaxGamepads ({})", gamepad,
                   kMaxGamepads);
    return m_gamepads[gamepad];
}

Input::Gamepad& Input::gamepadAt(u32 gamepad) {
    TESERRA_ASSERT(gamepad < kMaxGamepads, "Gamepad index {} is not below kMaxGamepads ({})", gamepad,
                   kMaxGamepads);
    return m_gamepads[gamepad];
}

void Input::setGamepadConnected(u32 gamepad, bool connected) {
    Gamepad& state = gamepadAt(gamepad);
    if (state.connected == connected) {
        return;
    }
    // Either way the gamepad starts from rest: a new one then reports what it holds, a removed one holds
    // nothing.
    state.released |= state.down;
    state.down.reset();
    state.axes = {};
    state.connected = connected;
}

void Input::setGamepadButton(u32 gamepad, GamepadButton button, bool down) {
    Gamepad& state = gamepadAt(gamepad);
    const usize index = indexOf(button);
    if (state.down.test(index) == down) {
        return;
    }
    state.down.set(index, down);
    (down ? state.pressed : state.released).set(index);
}

void Input::setKey(Key key, bool down) {
    if (key == Key::Unknown) {
        return;
    }
    const usize index = indexOf(key);
    // Only transitions create edges: repeats and duplicate events (a release after focus loss already
    // released the key) change nothing.
    if (m_keysDown.test(index) == down) {
        return;
    }
    m_keysDown.set(index, down);
    (down ? m_keysPressed : m_keysReleased).set(index);
}

void Input::setButton(MouseButton button, bool down) {
    const usize index = indexOf(button);
    if (m_buttonsDown.test(index) == down) {
        return;
    }
    m_buttonsDown.set(index, down);
    (down ? m_buttonsPressed : m_buttonsReleased).set(index);
}

void Input::moveMouse(Vec2 position) noexcept {
    if (m_hasMousePosition) {
        m_mouseDelta += position - m_mousePosition;
    }
    m_mousePosition = position;
    m_hasMousePosition = true;
}

void Input::appendText(char32_t codepoint) {
    const bool isSurrogate = codepoint >= 0xD800 && codepoint <= 0xDFFF;
    if (codepoint > 0x10FFFF || isSurrogate) {
        codepoint = 0xFFFD; // REPLACEMENT CHARACTER
    }
    // UTF-8: 1 byte up to U+007F, 2 up to U+07FF, 3 up to U+FFFF, 4 above.
    const auto byte = [](char32_t bits) { return static_cast<char>(static_cast<u8>(bits)); };
    if (codepoint < 0x80) {
        m_text += byte(codepoint);
    } else if (codepoint < 0x800) {
        m_text += byte(0xC0 | (codepoint >> 6));
        m_text += byte(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        m_text += byte(0xE0 | (codepoint >> 12));
        m_text += byte(0x80 | ((codepoint >> 6) & 0x3F));
        m_text += byte(0x80 | (codepoint & 0x3F));
    } else {
        m_text += byte(0xF0 | (codepoint >> 18));
        m_text += byte(0x80 | ((codepoint >> 12) & 0x3F));
        m_text += byte(0x80 | ((codepoint >> 6) & 0x3F));
        m_text += byte(0x80 | (codepoint & 0x3F));
    }
}

void Input::releaseAll() noexcept {
    m_keysReleased |= m_keysDown;
    m_keysDown.reset();
    m_buttonsReleased |= m_buttonsDown;
    m_buttonsDown.reset();
}

} // namespace teserra::platform
