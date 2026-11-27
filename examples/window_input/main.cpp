// Window and input: the Iteration 3 demo (#17). Opens a resizable window, logs keyboard, mouse, text and
// gamepad input, shows FPS and a position moved with WASD or a gamepad's left stick in the title, and quits
// on Escape or when the window is closed.
//
//   teserra_window_input                              runs until Escape or close
//   teserra_window_input --frames 120 --hidden-window smoke run: 120 frames in a hidden window

#include <teserra/core/log.hpp>
#include <teserra/core/math.hpp>
#include <teserra/engine/app.hpp>
#include <teserra/platform/event.hpp>
#include <teserra/platform/gamepad.hpp>
#include <teserra/platform/input.hpp>
#include <teserra/platform/key.hpp>
#include <teserra/platform/mouse_button.hpp>

#include <chrono>
#include <format>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>

using namespace teserra;
using namespace teserra::platform;

namespace {

LogCategory exampleCategory() {
    static const LogCategory kCategory = log::registerCategory("WindowInput");
    return kCategory;
}

void logEvent(const Event& event) {
    std::visit(
        [](const auto& e) {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, KeyChanged>) {
                if (!e.repeat) {
                    TESERRA_LOG_INFO(exampleCategory(), "Key {} {}", keyName(e.key), e.down ? "down" : "up");
                }
            } else if constexpr (std::is_same_v<E, MouseButtonChanged>) {
                TESERRA_LOG_INFO(exampleCategory(), "Mouse {} {}", mouseButtonName(e.button),
                                 e.down ? "down" : "up");
            } else if constexpr (std::is_same_v<E, MouseScrolled>) {
                TESERRA_LOG_INFO(exampleCategory(), "Scrolled ({}, {})", e.offset.x, e.offset.y);
            } else if constexpr (std::is_same_v<E, WindowResized>) {
                TESERRA_LOG_INFO(exampleCategory(), "Resized to {}x{} ({}x{} pixels)", e.windowSize.x,
                                 e.windowSize.y, e.framebufferSize.x, e.framebufferSize.y);
            } else if constexpr (std::is_same_v<E, FocusChanged>) {
                TESERRA_LOG_INFO(exampleCategory(), "Focus {}", e.focused ? "gained" : "lost");
            } else if constexpr (std::is_same_v<E, WindowCloseRequested>) {
                TESERRA_LOG_INFO(exampleCategory(), "Close requested");
            } else if constexpr (std::is_same_v<E, GamepadConnectionChanged>) {
                TESERRA_LOG_INFO(exampleCategory(), "Gamepad {} {}", e.gamepad,
                                 e.connected ? "connected" : "disconnected");
            } else if constexpr (std::is_same_v<E, GamepadButtonChanged>) {
                TESERRA_LOG_INFO(exampleCategory(), "Gamepad {} {} {}", e.gamepad,
                                 gamepadButtonName(e.button), e.down ? "down" : "up");
            }
            // MouseMoved and the gamepads' axes are shown in the title instead: they would flood the log.
            // TextEntered is logged per frame from Input::text(), already UTF-8.
        },
        event);
}

} // namespace

// Only std::bad_alloc can escape (from std::function and std::string); terminating is the right answer.
int main(int argc, char** argv) { // NOLINT(bugprone-exception-escape)
    engine::App app{{.title = "Teserra: window and input", .args = {argc, argv}}};

    Vec2 position{0.0F};           // moved with WASD in fixed steps
    constexpr f32 kSpeed = 100.0F; // units per second
    std::chrono::nanoseconds sinceTitle{0};

    const engine::AppCallbacks callbacks{
        .onEvent =
            [](engine::App& running, const Event& event) {
                logEvent(event);
                const auto* key = std::get_if<KeyChanged>(&event);
                if (key != nullptr && key->key == Key::Escape) {
                    running.quit();
                }
            },
        .fixedUpdate =
            [&](engine::App& running, f32 stepSeconds) {
                const Input& input = running.input();
                Vec2 direction = input.axis2D(Key::A, Key::D, Key::S, Key::W);
                if (const std::optional<u32> pad = input.firstGamepad()) {
                    direction += input.stick(*pad, GamepadStick::Left);
                }
                position += direction * kSpeed * stepSeconds;
            },
        .update =
            [&](engine::App& running, f32 /*deltaSeconds*/) {
                const Input& input = running.input();
                if (!input.text().empty()) {
                    TESERRA_LOG_INFO(exampleCategory(), "Typed \"{}\"", input.text());
                }

                // A title change is a window-system call; four times a second is enough to read.
                sinceTitle += running.frameTimer().delta();
                if (sinceTitle >= std::chrono::milliseconds{250}) {
                    sinceTitle = std::chrono::nanoseconds{0};
                    std::string title =
                        std::format("Teserra: window and input | {:.0f} FPS | cursor ({:.0f}, {:.0f}) | "
                                    "position ({:.0f}, {:.0f})",
                                    running.frameTimer().fps(), input.mousePosition().x,
                                    input.mousePosition().y, position.x, position.y);
                    if (const std::optional<u32> pad = input.firstGamepad()) {
                        const Vec2 left = input.stick(*pad, GamepadStick::Left);
                        const Vec2 right = input.stick(*pad, GamepadStick::Right);
                        title += std::format(
                            " | pad {}: L ({:.2f}, {:.2f}) R ({:.2f}, {:.2f}) LT {:.2f} RT {:.2f}", *pad,
                            left.x, left.y, right.x, right.y, input.axis(*pad, GamepadAxis::LeftTrigger),
                            input.axis(*pad, GamepadAxis::RightTrigger));
                    }
                    running.window().setTitle(title);
                }
            },
    };

    return app.run(callbacks);
}
