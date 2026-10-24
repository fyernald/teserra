#pragma once

// A platform without a window system for App tests: time comes from a ManualClock that advances by a
// scripted frame time on every pollEvents(), and events are scripted per poll.

#include <teserra/platform/clock.hpp>
#include <teserra/platform/file_system.hpp>
#include <teserra/platform/platform.hpp>
#include <teserra/platform/window.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace teserra::test {

/// Records lifetime events of headless platforms and windows, in order.
using Journal = std::vector<std::string>;

class HeadlessWindow final : public platform::Window {
public:
    HeadlessWindow(platform::WindowDesc desc, Journal* journal)
        : m_desc(std::move(desc)), m_journal(journal) {}
    // The journal may allocate; running out of memory in a test ends the run anyway.
    ~HeadlessWindow() override { record(m_journal, "window destroyed"); } // NOLINT(bugprone-exception-escape)

    HeadlessWindow(const HeadlessWindow&) = delete;
    HeadlessWindow& operator=(const HeadlessWindow&) = delete;
    HeadlessWindow(HeadlessWindow&&) = delete;
    HeadlessWindow& operator=(HeadlessWindow&&) = delete;

    [[nodiscard]] UVec2 windowSize() const override { return m_desc.size; }
    [[nodiscard]] UVec2 framebufferSize() const override { return m_desc.size; }
    [[nodiscard]] Vec2 contentScale() const override { return Vec2{1.0F}; }
    void setSize(UVec2 size) override { m_desc.size = size; }
    void setTitle(std::string_view title) override { m_desc.title = title; }
    [[nodiscard]] bool shouldClose() const override { return false; }
    [[nodiscard]] bool isFocused() const override { return true; }
    [[nodiscard]] platform::NativeWindowHandle nativeHandle() const override { return {}; }
    [[nodiscard]] platform::OpenGlContext* openGlContext() override { return nullptr; }

    static void record(Journal* journal, std::string entry) {
        if (journal != nullptr) {
            journal->push_back(std::move(entry));
        }
    }

private:
    platform::WindowDesc m_desc;
    Journal* m_journal;
};

class HeadlessPlatform final : public platform::Platform {
public:
    explicit HeadlessPlatform(Journal* journal = nullptr)
        : m_journal(journal) {}
    // NOLINTNEXTLINE(bugprone-exception-escape): see ~HeadlessWindow
    ~HeadlessPlatform() override { HeadlessWindow::record(m_journal, "platform destroyed"); }

    /// Every poll advances the clock by the next frame time; the last one repeats.
    void setFrameTimes(std::vector<std::chrono::nanoseconds> frameTimes) {
        m_frameTimes = std::move(frameTimes);
    }

    /// Delivers `event` from the `poll`-th pollEvents() call, counting from 1 (the first frame).
    void queueEvent(u64 poll, const platform::Event& event) { m_scriptedEvents[poll].push_back(event); }

    [[nodiscard]] Result<std::unique_ptr<platform::Window>>
    createWindow(const platform::WindowDesc& desc) override {
        m_lastWindowDesc = desc;
        return std::make_unique<HeadlessWindow>(desc, m_journal);
    }

    void pollEvents(platform::EventQueue& queue) override {
        const usize index = m_polls < m_frameTimes.size() ? m_polls : m_frameTimes.size() - 1;
        m_clock.advance(m_frameTimes[index]);
        ++m_polls;
        if (const auto events = m_scriptedEvents.find(m_polls); events != m_scriptedEvents.end()) {
            for (const platform::Event& event : events->second) {
                queue.push(event);
            }
        }
    }

    [[nodiscard]] const platform::Clock& clock() const noexcept override { return m_clock; }
    [[nodiscard]] platform::FileSystem& fileSystem() noexcept override { return m_fileSystem; }

    [[nodiscard]] u64 polls() const noexcept { return m_polls; }
    [[nodiscard]] const platform::WindowDesc& lastWindowDesc() const noexcept { return m_lastWindowDesc; }

private:
    Journal* m_journal;
    platform::ManualClock m_clock;
    platform::NativeFileSystem m_fileSystem;
    std::vector<std::chrono::nanoseconds> m_frameTimes{std::chrono::milliseconds{16}};
    std::map<u64, std::vector<platform::Event>> m_scriptedEvents;
    platform::WindowDesc m_lastWindowDesc;
    u64 m_polls = 0;
};

} // namespace teserra::test
