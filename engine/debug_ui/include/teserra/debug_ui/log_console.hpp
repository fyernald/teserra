#pragma once

// The debug UI's log console (#299): the last messages of the log, in the game, with level, category and
// text filters.
//
//   LogConsole console;                       // receives every message logged from here on
//   debugUi.addWindow("Log", [&] { console.draw(); });

#include <teserra/core/log.hpp>
#include <teserra/core/types.hpp>

#include <bitset>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace teserra::debugui {

/// A message of the log as the console keeps it.
struct LogEntry {
    /// Counts the messages the buffer received, from 0.
    u64 sequence = 0;
    LogLevel level = LogLevel::Info;
    LogCategory category = LogCategory::Core;
    std::string message;
    /// When it was received, in seconds since the buffer was created.
    f64 seconds = 0.0;
};

/// The last `capacity` messages, oldest dropped first. push() may be called from any thread (the log's
/// callback sink runs on the thread that logs); copyFrom() copies under the same lock.
class LogBuffer {
public:
    explicit LogBuffer(usize capacity);

    LogBuffer(const LogBuffer&) = delete;
    LogBuffer& operator=(const LogBuffer&) = delete;
    LogBuffer(LogBuffer&&) = delete;
    LogBuffer& operator=(LogBuffer&&) = delete;

    void push(const log::LogRecord& record);
    /// Appends to `out` the kept messages whose sequence is at least `first`, oldest first, and returns the
    /// sequence of the next message: pass it as `first` next time to get only newer ones.
    u64 copyFrom(u64 first, std::deque<LogEntry>& out) const;
    [[nodiscard]] usize capacity() const noexcept { return m_ring.size(); }

private:
    mutable std::mutex m_mutex;
    std::vector<LogEntry> m_ring; // m_ring[sequence % capacity]; strings keep their memory when reused
    u64 m_next = 0;
    std::chrono::steady_clock::time_point m_start = std::chrono::steady_clock::now();
};

/// Which messages the console shows: all three conditions must hold.
struct LogFilter {
    /// Messages below this level are hidden.
    LogLevel minimumLevel = LogLevel::Trace;
    /// Categories whose messages are hidden, by their value.
    std::bitset<log::kMaxCategories> hiddenCategories;
    /// Shown messages contain this text, ignoring case, in the message or the category's name; empty
    /// shows all.
    std::string text;

    [[nodiscard]] bool passes(const LogEntry& entry) const;
};

/// Receives every message logged while it is alive (through a log::CallbackSink into a LogBuffer) and
/// shows them in an ImGui window. Main thread, except for the sink.
class LogConsole {
public:
    static constexpr usize kDefaultCapacity = 2000;

    explicit LogConsole(usize capacity = kDefaultCapacity);

    LogConsole(const LogConsole&) = delete;
    LogConsole& operator=(const LogConsole&) = delete;
    LogConsole(LogConsole&&) = delete;
    LogConsole& operator=(LogConsole&&) = delete;

    /// Takes the messages received since the last update(), dropping the oldest past the capacity. draw()
    /// calls it.
    void update();
    /// Draws the console's contents into the current ImGui window: filters, Clear, and the messages that
    /// pass the filter, colored by level, scrolled to the newest unless the user scrolled up.
    void draw();
    /// Forgets the messages taken so far; newer ones still come.
    void clear() noexcept { m_entries.clear(); }

    /// The messages taken so far, oldest first.
    [[nodiscard]] const std::deque<LogEntry>& entries() const noexcept { return m_entries; }
    [[nodiscard]] LogFilter& filter() noexcept { return m_filter; }

private:
    LogBuffer m_buffer; // before m_sink, which writes to it until it is destroyed
    log::CallbackSink m_sink;
    std::deque<LogEntry> m_entries;
    u64 m_nextSequence = 0;
    LogFilter m_filter;
    std::vector<u32> m_shown; // indices of the entries that pass the filter, reused every frame
    bool m_followNewest = true;
};

} // namespace teserra::debugui
