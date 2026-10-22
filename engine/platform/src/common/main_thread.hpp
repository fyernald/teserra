#pragma once

#include <thread>

namespace teserra::platform {

/// Remembers the thread that created it, so that window-system functions can assert they run there.
class MainThread {
public:
    MainThread() noexcept
        : m_id(std::this_thread::get_id()) {}

    [[nodiscard]] bool isCurrent() const noexcept { return std::this_thread::get_id() == m_id; }

private:
    std::thread::id m_id;
};

} // namespace teserra::platform
