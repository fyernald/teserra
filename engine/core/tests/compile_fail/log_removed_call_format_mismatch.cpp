// Compile-fail fixture (core.log.removed_call_format_string_is_checked): calls removed by
// TESERRA_LOG_LEVEL_MIN still have their format string checked, so they cannot rot unnoticed.

#define TESERRA_LOG_LEVEL_MIN TESERRA_LOG_LEVEL_OFF

#include <teserra/core/log.hpp>

void logWithMissingArgument() {
    TESERRA_LOG_TRACE(teserra::LogCategory::Core, "{} {}", 1);
}
