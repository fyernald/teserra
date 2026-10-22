#pragma once

// Test helper: records every log message delivered while it is alive.

#include <teserra/core/log.hpp>
#include <teserra/core/types.hpp>

#include <string>
#include <vector>

namespace teserra::test {

struct CapturedLog {
    LogLevel level;
    LogCategory category;
    std::string categoryName;
    std::string message;
    std::string file;
    u32 line;
};

class LogCapture {
public:
    LogCapture()
        : m_sink([this](const log::LogRecord& record) {
              m_records.push_back({record.level, record.category, std::string(record.categoryName),
                                   std::string(record.message), record.location.file_name(),
                                   static_cast<u32>(record.location.line())});
          }) {}

    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    [[nodiscard]] const std::vector<CapturedLog>& records() const noexcept { return m_records; }
    void clear() noexcept { m_records.clear(); }

private:
    std::vector<CapturedLog> m_records; // declared before m_sink, which writes to it
    log::CallbackSink m_sink;
};

} // namespace teserra::test
