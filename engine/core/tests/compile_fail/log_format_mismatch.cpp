// Compile-fail fixture (core.log.format_string_is_checked): a format string that does not match its
// arguments must not compile (ADR-0011).

#include <teserra/core/log.hpp>

void logWithMissingArgument() {
    TESERRA_LOG_INFO(teserra::LogCategory::Core, "{} {}", 1);
}
