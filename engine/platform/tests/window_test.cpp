#include <teserra/platform/window.hpp>

#include <teserra/platform/opengl_context.hpp>
#include <teserra/platform/platform.hpp>
#include <teserra/test/assert.hpp>

#include "platform_test_support.hpp"
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <thread>
#include <variant>
#include <vector>

using namespace teserra;
using namespace teserra::platform;

namespace {

std::vector<WindowResized> resizesIn(const EventQueue& queue) {
    std::vector<WindowResized> found;
    for (const Event& event : queue) {
        if (const auto* resized = std::get_if<WindowResized>(&event)) {
            found.push_back(*resized);
        }
    }
    return found;
}

} // namespace

TEST_CASE("Platform: only one exists at a time", "[platform][window]") {
    const auto platform = test::createPlatformOrSkip();

    const Result<std::unique_ptr<Platform>> second = Platform::create();

    REQUIRE_FALSE(second.has_value());
    CHECK(second.error().code == ErrorCode::AlreadyExists);
}

TEST_CASE("Platform: can be created again after the previous one is destroyed", "[platform][window]") {
    {
        const auto first = test::createPlatformOrSkip();
    }
    const Result<std::unique_ptr<Platform>> second = Platform::create();

    CHECK(second.has_value());
}

TEST_CASE("Window: GraphicsApi::None has sizes, a native handle and no OpenGL context",
          "[platform][window]") {
    const auto platform = test::createPlatformOrSkip();
    const Result<std::unique_ptr<Window>> created = platform->createWindow(test::hiddenWindow());
    REQUIRE(created.has_value());
    const std::unique_ptr<Window>& window = *created;

    CHECK(window->openGlContext() == nullptr);
    CHECK(window->framebufferSize().x > 0);
    CHECK(window->framebufferSize().y > 0);
    CHECK(window->contentScale().x > 0.0F);
    CHECK_FALSE(window->shouldClose());

    const NativeWindowHandle handle = window->nativeHandle();
    CHECK(handle.kind != NativeWindowHandle::Kind::None);
    CHECK(handle.window != nullptr);

    window->setTitle("Renamed");
}

TEST_CASE("Window: setSize produces a WindowResized event", "[platform][window]") {
    const auto platform = test::createPlatformOrSkip();
    const auto window = platform->createWindow(test::hiddenWindow());
    REQUIRE(window.has_value());
    EventQueue queue;
    platform->pollEvents(queue);
    queue.clear();

    (*window)->setSize({400, 300});
    platform->pollEvents(queue);

    const std::vector<WindowResized> resizes = resizesIn(queue);
    REQUIRE_FALSE(resizes.empty());
    CHECK(resizes.back().windowSize == (*window)->windowSize());
    CHECK(resizes.back().framebufferSize == (*window)->framebufferSize());
    CHECK(resizes.back().framebufferSize.x > 0);
}

// Needs a GPU driver with OpenGL 4.5. Without one (e.g. CI runners), creation must fail with an error that
// names the reason, and the rest of the test is skipped.
TEST_CASE("Window: GraphicsApi::OpenGL creates a 4.5 core context or explains why not",
          "[platform][window]") {
    const auto platform = test::createPlatformOrSkip();
    const Result<std::unique_ptr<Window>> created =
        platform->createWindow(test::hiddenWindow(GraphicsApi::OpenGL));
    if (!created) {
        CHECK(created.error().code == ErrorCode::DeviceFailure);
        CHECK(created.error().message.find("OpenGL 4.5 core context") != std::string::npos);
        SKIP("No OpenGL 4.5 driver: " << std::format("{}", created.error()));
    }
    OpenGlContext* context = (*created)->openGlContext();
    REQUIRE(context != nullptr);

    context->makeCurrent();
    context->setSwapInterval(0);
    CHECK(context->getProcAddress("glClear") != nullptr);
    CHECK(context->getProcAddress("glCreateBuffers") != nullptr); // 4.5 direct state access
    CHECK(context->getProcAddress("glNoSuchFunction") == nullptr);
    context->swapBuffers();
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("Window: functions assert they run on the main thread", "[platform][window]") {
    const auto platform = test::createPlatformOrSkip();
    const auto window = platform->createWindow(test::hiddenWindow());
    REQUIRE(window.has_value());

    // The assertion is captured on the other thread; only its message comes back.
    std::string message;
    std::thread([&] {
        if (const auto failure = test::captureAssert([&] { static_cast<void>((*window)->windowSize()); })) {
            message = failure->message();
        }
    }).join();

    CHECK(message == "Window functions must be called on the main thread");
}
#endif
