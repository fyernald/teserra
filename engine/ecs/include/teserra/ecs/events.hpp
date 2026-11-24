#pragma once

// Typed events between systems (docs/architecture/ecs.md#resources-and-events): double-buffered, so events
// sent in frame N can be read during frames N and N+1 and are then dropped, and read through per-reader
// cursors, so every reader sees every event once.
//
//   ecs::addEvents<Hit>(world);                                     // once: the Events<Hit> resource
//   schedule.add(Phase::Update, "damage", damage).sendsEvents<Hit>();
//   schedule.add(Phase::Update, "sound", playHitSounds).readsEvents<Hit>();
//
//   void damage(ecs::SystemContext& ctx) { ctx.sendEvent(Hit{.target = enemy, .amount = 3}); }
//   void playHitSounds(ecs::SystemContext& ctx) { ctx.readEvents<Hit>([&](const Hit& hit) { ... }); }
//
// updateEvents(world) swaps the buffers of every registered event type once per frame; scenes do it at the
// start of PreUpdate.

#include <teserra/core/types.hpp>
#include <teserra/ecs/type_key.hpp>
#include <teserra/ecs/world.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace teserra::ecs {

/// Where a reader is in a stream of events: the id of the next event it has not read.
struct EventCursor {
    u64 next = 0;
};

/// The events of type T of the current and the previous frame: a resource. Events get increasing ids in
/// the order they are sent; buffers keep their capacity, so a steady-state frame allocates nothing.
template <typename T>
class Events {
public:
    /// Sends `event`: readable from now until the end of the next frame.
    void send(T event) { m_current.push_back(std::move(event)); }

    /// Starts the next frame: the previous frame's events are dropped and this frame's become the previous.
    void update() noexcept {
        std::swap(m_previous, m_current);
        m_current.clear();
        m_previousFirst = m_currentFirst;
        m_currentFirst = m_previousFirst + m_previous.size();
    }

    /// Calls f(const T&) for every event `cursor` has not seen, oldest first, and moves the cursor past them.
    /// Events dropped before the reader got to them are skipped. Returns how many it read.
    template <typename F>
    usize read(EventCursor& cursor, F&& f) const {
        usize count = 0;
        const u64 start = std::max(cursor.next, m_previousFirst);
        for (u64 id = start; id < m_currentFirst; ++id) {
            f(m_previous[id - m_previousFirst]);
            ++count;
        }
        for (u64 id = std::max(start, m_currentFirst); id < m_currentFirst + m_current.size(); ++id) {
            f(m_current[id - m_currentFirst]);
            ++count;
        }
        cursor.next = m_currentFirst + m_current.size();
        return count;
    }

    /// A cursor past every event sent so far: a reader that starts from it sees only later events.
    [[nodiscard]] EventCursor latest() const noexcept { return {.next = m_currentFirst + m_current.size()}; }

    /// The events still readable: the previous frame's and this frame's.
    [[nodiscard]] usize size() const noexcept { return m_previous.size() + m_current.size(); }
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    /// Drops every event; cursors stay valid.
    void clear() noexcept {
        update();
        update();
    }

private:
    std::vector<T> m_previous;
    std::vector<T> m_current;
    u64 m_previousFirst = 0; // id of m_previous[0]
    u64 m_currentFirst = 0;  // id of m_current[0]
};

/// The event types of a world that updateEvents() swaps: a resource that addEvents() maintains.
class EventRegistry {
public:
    using Update = void (*)(World& world);

    /// Registers `update` for `type` once.
    void add(TypeKey type, Update update) {
        if (std::ranges::none_of(m_updates, [&](const auto& entry) { return entry.first == type; })) {
            m_updates.emplace_back(type, update);
        }
    }

    void updateAll(World& world) const {
        for (const auto& [type, update] : m_updates) {
            update(world);
        }
    }

    [[nodiscard]] usize size() const noexcept { return m_updates.size(); }

private:
    std::vector<std::pair<TypeKey, Update>> m_updates;
};

/// The world's Events<T>, inserted and registered with updateEvents() on first use.
template <typename T>
Events<T>& addEvents(World& world) {
    if (Events<T>* existing = world.tryResource<Events<T>>()) {
        return *existing;
    }
    EventRegistry* registry = world.tryResource<EventRegistry>();
    if (registry == nullptr) {
        registry = &world.insertResource<EventRegistry>();
    }
    registry->add(typeKey<Events<T>>(), [](World& target) {
        if (Events<T>* events = target.tryResource<Events<T>>()) {
            events->update();
        }
    });
    return world.insertResource<Events<T>>();
}

/// Starts the next frame of every event type addEvents() registered with `world`.
inline void updateEvents(World& world) {
    if (const EventRegistry* registry = world.tryResource<EventRegistry>()) {
        registry->updateAll(world);
    }
}

} // namespace teserra::ecs
