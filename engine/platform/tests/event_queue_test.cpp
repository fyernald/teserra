#include <teserra/platform/event.hpp>

#include <catch2/catch_test_macros.hpp>

#include <variant>
#include <vector>

using namespace teserra;
using namespace teserra::platform;

TEST_CASE("EventQueue: keeps events in order", "[platform][event]") {
    EventQueue queue;
    CHECK(queue.empty());

    queue.push(FocusChanged{true});
    queue.push(WindowResized{{800, 600}, {1600, 1200}});
    queue.push(WindowCloseRequested{});

    REQUIRE(queue.size() == 3);
    const std::vector<Event> expected{FocusChanged{true}, WindowResized{{800, 600}, {1600, 1200}},
                                      WindowCloseRequested{}};
    CHECK(std::vector<Event>(queue.begin(), queue.end()) == expected);
    CHECK(std::get<WindowResized>(queue.events()[1]).framebufferSize == UVec2(1600, 1200));
}

TEST_CASE("EventQueue: clear keeps the storage for the next frame", "[platform][event]") {
    EventQueue queue;
    for (int i = 0; i < 16; ++i) {
        queue.push(FocusChanged{i % 2 == 0});
    }
    const Event* storage = queue.events().data();

    queue.clear();
    CHECK(queue.empty());
    queue.push(WindowCloseRequested{});

    // Same buffer: steady-state polling does not allocate.
    CHECK(queue.events().data() == storage);
}

TEST_CASE("EventQueue: events dispatch with std::visit", "[platform][event]") {
    EventQueue queue;
    queue.push(WindowResized{{640, 480}, {640, 480}});
    queue.push(FocusChanged{false});
    queue.push(WindowCloseRequested{});

    int resized = 0;
    int focus = 0;
    int close = 0;
    for (const Event& event : queue) {
        std::visit(
            [&](const auto& e) {
                using E = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<E, WindowResized>) {
                    ++resized;
                } else if constexpr (std::is_same_v<E, FocusChanged>) {
                    ++focus;
                } else {
                    ++close;
                }
            },
            event);
    }
    CHECK(resized == 1);
    CHECK(focus == 1);
    CHECK(close == 1);
}
