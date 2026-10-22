#include <teserra/platform/input.hpp>

#include <teserra/core/assert.hpp>

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

f32 axis(bool negative, bool positive) noexcept {
    return (positive ? 1.0F : 0.0F) - (negative ? 1.0F : 0.0F);
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
    const Vec2 direction{axis(isDown(left), isDown(right)), axis(isDown(down), isDown(up))};
    // Both axes non-zero is the only case longer than 1.
    return direction.x != 0.0F && direction.y != 0.0F ? math::normalize(direction) : direction;
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
