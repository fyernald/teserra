// Events: double-buffered (readable in the frame they are sent and the next, then dropped), read through
// per-reader cursors (two readers each see every event once), declared in the schedule, and allocation-free
// in a steady state.

#include <teserra/ecs/events.hpp>

#include <teserra/ecs/schedule.hpp>
#include <teserra/ecs/system_context.hpp>
#include <teserra/ecs/world.hpp>
#include <teserra/test/allocation_counter.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace teserra;
using namespace teserra::ecs;

namespace {

struct Hit {
    int amount = 0;
};

std::vector<int> readAll(const Events<Hit>& events, EventCursor& cursor) {
    std::vector<int> amounts;
    events.read(cursor, [&](const Hit& hit) { amounts.push_back(hit.amount); });
    return amounts;
}

} // namespace

TEST_CASE("Events: sent in frame N, readable in N and N+1, then dropped", "[ecs][events]") {
    Events<Hit> events;
    events.send(Hit{.amount = 1}); // frame 0
    events.send(Hit{.amount = 2});

    EventCursor late;  // reads only in frame 1
    EventCursor never; // never reads
    static_cast<void>(never);
    EventCursor now;
    CHECK(readAll(events, now) == std::vector<int>{1, 2});

    events.update(); // frame 1
    events.send(Hit{.amount = 3});
    CHECK(events.size() == 3);
    CHECK(readAll(events, now) == std::vector<int>{3}); // only what it has not seen
    CHECK(readAll(events, late) == std::vector<int>{1, 2, 3});

    events.update(); // frame 2: frame 0's events are gone
    CHECK(events.size() == 1);
    EventCursor fresh;
    CHECK(readAll(events, fresh) == std::vector<int>{3});
    CHECK(readAll(events, now).empty());

    events.update(); // frame 3: everything is gone
    CHECK(events.empty());
    CHECK(readAll(events, never).empty());
}

TEST_CASE("Events: latest() skips what was sent, clear() drops everything", "[ecs][events]") {
    Events<Hit> events;
    events.send(Hit{.amount = 1});
    EventCursor fromNow = events.latest();
    events.send(Hit{.amount = 2});
    CHECK(readAll(events, fromNow) == std::vector<int>{2});

    events.clear();
    CHECK(events.empty());
    events.send(Hit{.amount = 4});
    CHECK(readAll(events, fromNow) == std::vector<int>{4});
}

TEST_CASE("Events: two reading systems each see every event once", "[ecs][events]") {
    World world;
    addEvents<Hit>(world);
    CHECK(&addEvents<Hit>(world) == &world.resource<Events<Hit>>()); // once

    std::vector<int> first;
    std::vector<int> second;
    int frame = 0;
    Schedule schedule;
    schedule.add(Phase::Update, "send", [&](SystemContext& ctx) { ctx.sendEvent(Hit{.amount = frame}); })
        .sendsEvents<Hit>();
    schedule
        .add(Phase::Update, "first",
             [&](SystemContext& ctx) {
                 ctx.readEvents<Hit>([&](const Hit& hit) { first.push_back(hit.amount); });
             })
        .readsEvents<Hit>()
        .after("send");
    // Runs before the sender: it sees each frame's event in the next frame.
    schedule
        .add(Phase::Update, "second",
             [&](SystemContext& ctx) {
                 ctx.readEvents<Hit>([&](const Hit& hit) { second.push_back(hit.amount); });
             })
        .readsEvents<Hit>()
        .before("send");

    for (frame = 0; frame < 3; ++frame) {
        updateEvents(world);
        schedule.run(Phase::Update, world);
    }
    CHECK(first == std::vector<int>{0, 1, 2});
    CHECK(second == std::vector<int>{0, 1});
}

#if TESERRA_ENABLE_ASSERTS

TEST_CASE("Events: sending and reading need declarations", "[ecs][events][validation]") {
    World world;
    addEvents<Hit>(world);
    Schedule schedule;
    schedule.add(Phase::Update, "sender", [](SystemContext& ctx) { ctx.sendEvent(Hit{}); });
    CHECK(
        test::requireAssert([&] { schedule.run(Phase::Update, world); }).message().find("writes resource") !=
        std::string::npos);

    Schedule readers;
    readers.add(Phase::Update, "reader",
                [](SystemContext& ctx) { static_cast<void>(ctx.readEvents<Hit>([](const Hit&) {})); });
    CHECK(test::requireAssert([&] { readers.run(Phase::Update, world); }).message().find("reads resource") !=
          std::string::npos);
}

#endif

TEST_CASE("Events: a steady-state frame allocates nothing", "[ecs][events][allocation]") {
    World world;
    addEvents<Hit>(world);
    int total = 0;
    Schedule schedule;
    schedule
        .add(Phase::Update, "send",
             [](SystemContext& ctx) {
                 for (int i = 0; i < 8; ++i) {
                     ctx.sendEvent(Hit{.amount = 1});
                 }
             })
        .sendsEvents<Hit>();
    schedule
        .add(Phase::Update, "read",
             [&](SystemContext& ctx) { ctx.readEvents<Hit>([&](const Hit& hit) { total += hit.amount; }); })
        .readsEvents<Hit>()
        .after("send");
    for (int frame = 0; frame < 3; ++frame) {
        updateEvents(world);
        schedule.run(Phase::Update, world);
    }

    u64 allocations = 0;
    {
        const test::AllocationCounter counter;
        updateEvents(world);
        schedule.run(Phase::Update, world);
        allocations = counter.count();
    }
    CHECK(allocations == 0);
    CHECK(total == 32);
}
