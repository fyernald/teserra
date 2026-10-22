#include <teserra/core/log.hpp>

#include <teserra/core/assert.hpp>

#include <spdlog/common.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <filesystem>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace teserra::log {

namespace {

constexpr std::array<std::string_view, 9> kEngineCategoryNames = {
    "Core", "Platform", "Rhi", "Renderer", "Assets", "Ecs", "Scene", "App", "Game",
};
static_assert(kEngineCategoryNames.size() == static_cast<usize>(LogCategory::Game) + 1,
              "every engine LogCategory needs a name");
static_assert(kMaxCategories <= usize{256}, "LogCategory is stored in a u8");

constexpr LogLevel kDefaultLevel = TESERRA_DEBUG_BUILD ? LogLevel::Debug : LogLevel::Info;

// Messages up to this size are formatted on the stack; longer ones allocate.
constexpr usize kInlineMessageSize = 1024;

constexpr const char* kConsolePattern = "[%H:%M:%S.%e] [%^%l%$] [%n] %v";
constexpr const char* kFilePattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] [thread %t] %v (%s:%#)";

// Set while callback sinks run on this thread, so that a callback that logs drops the message instead
// of deadlocking on the log mutex.
thread_local bool isInCallback = false;

spdlog::level::level_enum toSpdlog(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace:
            return spdlog::level::trace;
        case LogLevel::Debug:
            return spdlog::level::debug;
        case LogLevel::Info:
            return spdlog::level::info;
        case LogLevel::Warn:
            return spdlog::level::warn;
        case LogLevel::Error:
            return spdlog::level::err;
        case LogLevel::Critical:
            return spdlog::level::critical;
        case LogLevel::Off:
            break;
    }
    return spdlog::level::off;
}

template <usize... Indices>
constexpr std::array<std::atomic<LogLevel>, sizeof...(Indices)> makeLevels(std::index_sequence<Indices...>) {
    return {{(static_cast<void>(Indices), kDefaultLevel)...}};
}

// Category names and runtime levels. Constant-initialized and allocation-free, so the functions that
// only read it are noexcept and work at any point of static initialization. Reads take no lock: levels
// are atomics, and a category's name is written once, before the release store that publishes it.
class CategoryTable {
public:
    constexpr CategoryTable() noexcept
        : m_levels(makeLevels(std::make_index_sequence<kMaxCategories>{})) {
        std::copy(kEngineCategoryNames.begin(), kEngineCategoryNames.end(), m_names.begin());
    }

    // Returns the category and whether `name` could be registered; the caller logs the failure.
    std::pair<LogCategory, bool> registerCategory(std::string_view name) {
        if (name.empty() || name.size() > kMaxCategoryNameLength) {
            return {LogCategory::Game, false};
        }
        const std::lock_guard lock(m_mutex);
        const usize count = m_count.load(std::memory_order_relaxed);
        for (usize i = 0; i < count; ++i) {
            if (m_names[i] == name) {
                return {static_cast<LogCategory>(i), true};
            }
        }
        if (count == kMaxCategories) {
            return {LogCategory::Game, false};
        }
        std::array<char, kMaxCategoryNameLength>& storage = m_nameStorage[count];
        std::copy(name.begin(), name.end(), storage.begin());
        m_names[count] = std::string_view(storage.data(), name.size());
        m_count.store(count + 1, std::memory_order_release);
        return {static_cast<LogCategory>(count), true};
    }

    [[nodiscard]] std::string_view name(LogCategory category) const noexcept {
        const auto index = static_cast<usize>(category);
        return index < m_count.load(std::memory_order_acquire) ? m_names[index] : "Unknown";
    }

    void setLevel(LogCategory category, LogLevel level) noexcept {
        const auto index = static_cast<usize>(category);
        if (index < kMaxCategories) {
            m_levels[index].store(level, std::memory_order_relaxed);
        }
    }

    void setLevel(LogLevel level) noexcept {
        for (std::atomic<LogLevel>& categoryLevel : m_levels) {
            categoryLevel.store(level, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] LogLevel level(LogCategory category) const noexcept {
        const auto index = static_cast<usize>(category);
        return index < kMaxCategories ? m_levels[index].load(std::memory_order_relaxed) : LogLevel::Off;
    }

private:
    std::array<std::atomic<LogLevel>, kMaxCategories> m_levels;
    std::array<std::string_view, kMaxCategories> m_names{};
    std::array<std::array<char, kMaxCategoryNameLength>, kMaxCategories> m_nameStorage{};
    std::atomic<usize> m_count = kEngineCategoryNames.size();
    std::mutex m_mutex; // serializes registration
};

constinit CategoryTable categories;

// Character container for std::back_inserter that keeps the first kInlineMessageSize characters and
// counts the rest, so one formatting pass tells whether the message fit.
class TruncatingBuffer {
public:
    using value_type = char; // NOLINT(readability-identifier-naming): required by std::back_insert_iterator

    void push_back(char c) noexcept { // NOLINT(readability-identifier-naming): as above
        if (m_size < m_data.size()) {
            m_data[m_size] = c;
        }
        ++m_size;
    }

    [[nodiscard]] bool truncated() const noexcept { return m_size > m_data.size(); }
    [[nodiscard]] std::string_view view() const noexcept { return {m_data.data(), m_size}; }

private:
    std::array<char, kInlineMessageSize> m_data; // only the first m_size characters are written
    usize m_size = 0;
};

// Marks this thread as running callback sinks for its lifetime, also when a callback throws.
class CallbackScope {
public:
    CallbackScope() noexcept { isInCallback = true; }
    ~CallbackScope() { isInCallback = false; }

    CallbackScope(const CallbackScope&) = delete;
    CallbackScope& operator=(const CallbackScope&) = delete;
};

// Path as UTF-8 for messages. path::string() would convert to the ANSI code page on Windows and throw
// for characters it cannot represent.
std::string toUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

spdlog::sink_ptr makeConsoleSink() {
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sink->set_pattern(kConsolePattern);
    return sink;
}

struct Subscriber {
    u32 id;
    CallbackSink::Callback callback;
};

// Outputs: spdlog sinks and callback subscribers, behind one mutex.
class Outputs {
public:
    Outputs() { m_sinks.push_back(makeConsoleSink()); }
    ~Outputs() { flush(); }

    Outputs(const Outputs&) = delete;
    Outputs& operator=(const Outputs&) = delete;
    Outputs(Outputs&&) = delete;
    Outputs& operator=(Outputs&&) = delete;

    void setSinks(std::vector<spdlog::sink_ptr> sinks) {
        const std::lock_guard lock(m_mutex);
        for (const spdlog::sink_ptr& sink : m_sinks) {
            sink->flush();
        }
        m_sinks = std::move(sinks);
    }

    void write(LogLevel level, LogCategory category, std::source_location location,
               std::string_view message) {
        const std::lock_guard lock(m_mutex);
        const std::string_view categoryName = categories.name(category);

        if (!m_sinks.empty()) {
            const spdlog::details::log_msg spdlogMessage(
                spdlog::source_loc{location.file_name(), static_cast<int>(location.line()),
                                   location.function_name()},
                spdlog::string_view_t(categoryName.data(), categoryName.size()), toSpdlog(level),
                spdlog::string_view_t(message.data(), message.size()));
            for (const spdlog::sink_ptr& sink : m_sinks) {
                sink->log(spdlogMessage);
                // Warnings and errors reach the file even if the process dies right after.
                if (level >= LogLevel::Warn) {
                    sink->flush();
                }
            }
        }

        if (!m_subscribers.empty()) {
            const LogRecord record{level, category, categoryName, message, location};
            const CallbackScope scope;
            for (const Subscriber& subscriber : m_subscribers) {
                subscriber.callback(record);
            }
        }
    }

    void flush() {
        const std::lock_guard lock(m_mutex);
        for (const spdlog::sink_ptr& sink : m_sinks) {
            sink->flush();
        }
    }

    u32 subscribe(CallbackSink::Callback callback) {
        const std::lock_guard lock(m_mutex);
        const u32 id = m_nextSubscriberId++;
        m_subscribers.push_back({id, std::move(callback)});
        return id;
    }

    void unsubscribe(u32 id) {
        const std::lock_guard lock(m_mutex);
        std::erase_if(m_subscribers, [id](const Subscriber& subscriber) { return subscriber.id == id; });
    }

private:
    std::mutex m_mutex;
    std::vector<spdlog::sink_ptr> m_sinks;
    std::vector<Subscriber> m_subscribers;
    u32 m_nextSubscriberId = 1;
};

Outputs& outputs() {
    // Created on first use, so logging works from static initializers and before configure().
    static Outputs instance;
    return instance;
}

} // namespace

LogCategory registerCategory(std::string_view name) {
    const auto [category, registered] = categories.registerCategory(name);
    if (!registered) {
        TESERRA_LOG_WARN(LogCategory::Core,
                         "Log category '{}' could not be registered (empty, longer than {} characters, or "
                         "more than {} categories); using Game",
                         name, kMaxCategoryNameLength, kMaxCategories);
    }
    return category;
}

std::string_view categoryName(LogCategory category) noexcept {
    return categories.name(category);
}

void setLevel(LogCategory category, LogLevel level) noexcept {
    categories.setLevel(category, level);
}

void setLevel(LogLevel level) noexcept {
    categories.setLevel(level);
}

LogLevel level(LogCategory category) noexcept {
    return categories.level(category);
}

bool isEnabled(LogCategory category, LogLevel messageLevel) noexcept {
    return messageLevel != LogLevel::Off && messageLevel >= categories.level(category);
}

Result<void> configure(const LogConfig& config) {
    std::vector<spdlog::sink_ptr> sinks;
    if (config.console) {
        sinks.push_back(makeConsoleSink());
    }

    std::string fileError;
    if (!config.filePath.empty()) {
        // spdlog reports failures with exceptions, and path::string() throws for names the ANSI code page
        // cannot represent; this is the third-party boundary (ADR-0016).
        try {
            auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                config.filePath.string(), config.maxFileSize, config.maxFiles);
            sink->set_pattern(kFilePattern);
            sinks.push_back(std::move(sink));
        } catch (const std::exception& error) {
            fileError = error.what();
        }
    }

    outputs().setSinks(std::move(sinks));

    if (!fileError.empty()) {
        return fail(ErrorCode::IoFailure, Module::Core, "Cannot open log file '{}': {}",
                    toUtf8(config.filePath), fileError);
    }
    return {};
}

void flush() {
    outputs().flush();
}

CallbackSink::CallbackSink(Callback callback) {
    TESERRA_ASSERT(callback != nullptr, "CallbackSink needs a callback");
    m_id = outputs().subscribe(std::move(callback));
}

CallbackSink::~CallbackSink() {
    release();
}

CallbackSink::CallbackSink(CallbackSink&& other) noexcept
    : m_id(std::exchange(other.m_id, 0)) {
}

CallbackSink& CallbackSink::operator=(CallbackSink&& other) noexcept {
    if (this != &other) {
        release();
        m_id = std::exchange(other.m_id, 0);
    }
    return *this;
}

// The only throwing path clang-tidy finds is creating outputs(), which subscribe() did before m_id became
// non-zero.
// NOLINTNEXTLINE(bugprone-exception-escape)
void CallbackSink::release() noexcept {
    if (m_id != 0) {
        outputs().unsubscribe(m_id);
        m_id = 0;
    }
}

namespace detail {

void vwrite(LogLevel level, LogCategory category, std::source_location location, std::string_view format,
            std::format_args args) {
    if (isInCallback) {
        return;
    }

    TruncatingBuffer buffer;
    std::vformat_to(std::back_inserter(buffer), format, args);
    if (!buffer.truncated()) {
        outputs().write(level, category, location, buffer.view());
        return;
    }
    // Rare: long messages such as shader compiler output. The arguments are still alive, so format again.
    const std::string message = std::vformat(format, args);
    outputs().write(level, category, location, message);
}

} // namespace detail

} // namespace teserra::log
