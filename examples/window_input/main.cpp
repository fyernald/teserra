// Window and input: the Iteration 3 demo (#17). Opens a resizable window, logs keyboard, mouse and text
// input, shows FPS and a WASD-controlled position in the title, and quits on Escape or when the window is
// closed.
//
//   teserra_window_input                              runs until Escape or close
//   teserra_window_input --frames 120 --hidden-window smoke run: 120 frames in a hidden window

#include <teserra/core/log.hpp>
#include <teserra/core/math.hpp>
#include <teserra/engine/app.hpp>
#include <teserra/platform/event.hpp>
#include <teserra/platform/input.hpp>
#include <teserra/platform/key.hpp>
#include <teserra/platform/mouse_button.hpp>

#include <chrono>
#include <format>
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
            }
            // MouseMoved is shown in the title instead: it would flood the log. TextEntered is logged
            // per frame from Input::text(), already UTF-8.
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
            [](engine::App& app, const Event& event) {
                logEvent(event);
                const auto* key = std::get_if<KeyChanged>(&event);
                if (key != nullptr && key->key == Key::Escape) {
                    app.quit();
                }
            },
        .fixedUpdate =
            [&](engine::App& app, f32 stepSeconds) {
                position += app.input().axis2D(Key::A, Key::D, Key::S, Key::W) * kSpeed * stepSeconds;
            },
        .update =
            [&](engine::App& app, f32 /*deltaSeconds*/) {
                const Input& input = app.input();
                if (!input.text().empty()) {
                    TESERRA_LOG_INFO(exampleCategory(), "Typed \"{}\"", input.text());
                }

                // A title change is a window-system call; four times a second is enough to read.
                sinceTitle += app.frameTimer().delta();
                if (sinceTitle >= std::chrono::milliseconds{250}) {
                    sinceTitle = std::chrono::nanoseconds{0};
                    app.window().setTitle(
                        std::format("Teserra: window and input | {:.0f} FPS | cursor ({:.0f}, {:.0f}) | "
                                    "WASD position ({:.0f}, {:.0f})",
                                    app.frameTimer().fps(), input.mousePosition().x, input.mousePosition().y,
                                    position.x, position.y));
                }
            },
    };

    return app.run(callbacks);
}
