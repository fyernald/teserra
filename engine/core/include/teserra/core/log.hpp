#pragma once

// Logging facade (ADR-0011). Engine and game code log through the TESERRA_LOG_* macros:
//
//   TESERRA_LOG_INFO(teserra::LogCategory::Renderer, "Created swapchain {}x{}", width, height);
//
// Format strings are std::format strings checked at compile time. Calls below the compile-time floor
// TESERRA_LOG_LEVEL_MIN generate no code and do not evaluate their arguments; the rest are filtered by
// a runtime level per category before any formatting happens. spdlog is a private dependency of
// teserra_core: no spdlog or fmt header is reachable from here.

#include <teserra/core/config.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>

#include <filesystem>
#include <format>
#include <functional>
#include <source_location>
#include <string_view>

// Level values for TESERRA_LOG_LEVEL_MIN, usable in #if. They match teserra::LogLevel.
#define TESERRA_LOG_LEVEL_TRACE 0
#define TESERRA_LOG_LEVEL_DEBUG 1
#define TESERRA_LOG_LEVEL_INFO 2
#define TESERRA_LOG_LEVEL_WARN 3
#define TESERRA_LOG_LEVEL_ERROR 4
#define TESERRA_LOG_LEVEL_CRITICAL 5
#define TESERRA_LOG_LEVEL_OFF 6

// Compile-time floor: calls below it are removed entirely. Defaults to Trace in Debug builds and Info
// otherwise. Override it per target with a compile definition, e.g. TESERRA_LOG_LEVEL_MIN=3 (Warn); it
// only affects the translation units compiled with it.
#if !defined(TESERRA_LOG_LEVEL_MIN)
#if TESERRA_DEBUG_BUILD
#define TESERRA_LOG_LEVEL_MIN TESERRA_LOG_LEVEL_TRACE
#else
#define TESERRA_LOG_LEVEL_MIN TESERRA_LOG_LEVEL_INFO
#endif
#endif

namespace teserra {

/// Severity of a log message, from most to least verbose. Off is only valid as a filter level.
enum class LogLevel : u8 {
    Trace = TESERRA_LOG_LEVEL_TRACE,       ///< Per-frame detail; compiled out in Release.
    Debug = TESERRA_LOG_LEVEL_DEBUG,       ///< Diagnostics useful during development.
    Info = TESERRA_LOG_LEVEL_INFO,         ///< Lifecycle events (device created, scene loaded).
    Warn = TESERRA_LOG_LEVEL_WARN,         ///< Recoverable surprises.
    Error = TESERRA_LOG_LEVEL_ERROR,       ///< Handled failures.
    Critical = TESERRA_LOG_LEVEL_CRITICAL, ///< Just before TESERRA_FATAL.
    Off = TESERRA_LOG_LEVEL_OFF,           ///< Filter level that disables a category.
};

/// Subsystem a message belongs to. The enumerators are the engine's categories; games add their own
/// with log::registerCategory(), which returns further values of this type.
enum class LogCategory : u8 {
    Core,
    Platform,
    Rhi,
    Renderer,
    Assets,
    Ecs,
    Scene,
    App,
    Game,
};

namespace log {

/// Maximum number of categories, engine categories included.
inline constexpr usize kMaxCategories = 64;

/// Maximum length of a registered category name, in bytes.
inline constexpr usize kMaxCategoryNameLength = 32;

/// Returns the category called `name`, registering it on first use. Registering an existing name
/// (including an engine category such as "Renderer") returns the same category. An empty or too long
/// name, or a new name once kMaxCategories are registered, returns LogCategory::Game and logs a
/// warning. Thread-safe.
[[nodiscard]] LogCategory registerCategory(std::string_view name);

/// Name of a category ("Renderer", or the name it was registered with). Unregistered values return
/// "Unknown". The view stays valid for the lifetime of the program. Thread-safe.
[[nodiscard]] std::string_view categoryName(LogCategory category) noexcept;

/// Runtime level of a category: messages below it are dropped before they are formatted. Defaults
/// to Debug in Debug builds and Info otherwise. It cannot bring back calls removed by
/// TESERRA_LOG_LEVEL_MIN. Thread-safe.
void setLevel(LogCategory category, LogLevel level) noexcept;

/// Sets the runtime level of every category, including ones registered later. Thread-safe.
void setLevel(LogLevel level) noexcept;

/// Current runtime level of a category. Thread-safe.
[[nodiscard]] LogLevel level(LogCategory category) noexcept;

/// True when a message of `messageLevel` in `category` passes the runtime level. The TESERRA_LOG_*
/// macros check this before formatting. Thread-safe.
[[nodiscard]] bool isEnabled(LogCategory category, LogLevel messageLevel) noexcept;

/// Output configuration. Until configure() is called, messages go to the console only.
struct LogConfig {
    /// Colored output on stdout.
    bool console = true;
    /// Rotating log file; empty for none. Missing directories are created.
    std::filesystem::path filePath{};
    /// Size at which the file is rotated (renamed to <name>.1.<ext>) and a new one is started.
    usize maxFileSize = usize{5} * 1024 * 1024;
    /// Number of rotated files kept next to the current one.
    usize maxFiles = 3;
};

/// Replaces the console and file outputs. Callback sinks are not affected. Fails with
/// ErrorCode::IoFailure if the log file cannot be opened; the other outputs are still set up. The
/// failure is not logged here: the caller logs it once, where it is handled (ADR-0016).
/// Thread-safe, but meant to be called once at startup.
[[nodiscard]] Result<void> configure(const LogConfig& config);

/// Writes buffered output (the log file) to disk. Thread-safe.
void flush();

/// A message as delivered to a CallbackSink. The views are only valid during the callback.
struct LogRecord {
    LogLevel level;
    LogCategory category;
    std::string_view categoryName;
    std::string_view message;
    std::source_location location;
};

/// Receives every message that passes the filters while it is alive, e.g. for tests or an in-game
/// console. Callbacks run on the logging thread with the log lock held: they must not log, and must
/// not create or destroy sinks (messages logged from a callback are dropped). Move-only.
class CallbackSink {
public:
    using Callback = std::function<void(const LogRecord&)>;

    /// Starts delivering messages to `callback`, which must not be empty (asserted).
    explicit CallbackSink(Callback callback);
    ~CallbackSink();

    CallbackSink(const CallbackSink&) = delete;
    CallbackSink& operator=(const CallbackSink&) = delete;
    CallbackSink(CallbackSink&& other) noexcept;
    CallbackSink& operator=(CallbackSink&& other) noexcept;

private:
    void release() noexcept;

    u32 m_id = 0; // 0 when moved from
};

namespace detail {

/// Formats a message and sends it to every output. Called by the macros after the level checks.
void vwrite(LogLevel level, LogCategory category, std::source_location location, std::string_view format,
            std::format_args args);

template <typename... Args>
void write(LogLevel level, LogCategory category, std::source_location location,
           std::format_string<Args...> format, Args&&... args) {
    // The arguments are type-erased here so that formatting itself is not a template: one function
    // in log.cpp serves every call site.
    vwrite(level, category, location, format.get(), std::make_format_args(args...));
}

} // namespace detail

} // namespace log

} // namespace teserra

// Shared implementation of the level macros. `levelValue` is the TESERRA_LOG_LEVEL_* number, compared
// with `if constexpr` so that removed calls still have their format string checked.
#define TESERRA_DETAIL_LOG_AT(levelValue, logLevel, category, ...)                                           \
    do {                                                                                                     \
        if constexpr ((levelValue) >= TESERRA_LOG_LEVEL_MIN) {                                               \
            const ::teserra::LogCategory teserraLogCategory = (category);                                    \
            if (::teserra::log::isEnabled(teserraLogCategory, (logLevel))) {                                 \
                ::teserra::log::detail::write((logLevel), teserraLogCategory,                                \
                                              ::std::source_location::current(), __VA_ARGS__);               \
            }                                                                                                \
        }                                                                                                    \
    } while (false)

/// TESERRA_LOG_<LEVEL>(category, format, args...): logs a std::format message in `category`.
#define TESERRA_LOG_TRACE(category, ...)                                                                     \
    TESERRA_DETAIL_LOG_AT(TESERRA_LOG_LEVEL_TRACE, ::teserra::LogLevel::Trace, category, __VA_ARGS__)
#define TESERRA_LOG_DEBUG(category, ...)                                                                     \
    TESERRA_DETAIL_LOG_AT(TESERRA_LOG_LEVEL_DEBUG, ::teserra::LogLevel::Debug, category, __VA_ARGS__)
#define TESERRA_LOG_INFO(category, ...)                                                                      \
    TESERRA_DETAIL_LOG_AT(TESERRA_LOG_LEVEL_INFO, ::teserra::LogLevel::Info, category, __VA_ARGS__)
#define TESERRA_LOG_WARN(category, ...)                                                                      \
    TESERRA_DETAIL_LOG_AT(TESERRA_LOG_LEVEL_WARN, ::teserra::LogLevel::Warn, category, __VA_ARGS__)
#define TESERRA_LOG_ERROR(category, ...)                                                                     \
    TESERRA_DETAIL_LOG_AT(TESERRA_LOG_LEVEL_ERROR, ::teserra::LogLevel::Error, category, __VA_ARGS__)
#define TESERRA_LOG_CRITICAL(category, ...)                                                                  \
    TESERRA_DETAIL_LOG_AT(TESERRA_LOG_LEVEL_CRITICAL, ::teserra::LogLevel::Critical, category, __VA_ARGS__)
