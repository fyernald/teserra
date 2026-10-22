#include <teserra/core/assert.hpp>

#include <teserra/core/log.hpp>

#include <atomic>
#include <cstdlib>
#include <string>

// Review note (ADR-0015): platform code outside engine/platform/. TESERRA_FATAL breaks into an attached
// debugger (ADR-0016), and teserra_core is layer 0, below teserra_platform, so the two OS calls it needs
// are made here.
#if TESERRA_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN // NOLINT(readability-identifier-naming): Windows SDK configuration macro
#define NOMINMAX            // NOLINT(readability-identifier-naming): Windows SDK configuration macro
#include <intrin.h>
#include <windows.h>
#elif TESERRA_PLATFORM_LINUX
#include <csignal>
#include <fstream>
#endif

namespace teserra {

namespace {

bool isDebuggerAttached() noexcept {
#if TESERRA_PLATFORM_WINDOWS
    return IsDebuggerPresent() != 0;
#elif TESERRA_PLATFORM_LINUX
    // A tracing debugger shows up as a non-zero TracerPid.
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.starts_with("TracerPid:")) {
            return line.find_first_not_of(" \t0", sizeof("TracerPid:") - 1) != std::string::npos;
        }
    }
    return false;
#else
    return false;
#endif
}

void breakIntoDebugger() noexcept {
#if TESERRA_PLATFORM_WINDOWS
    __debugbreak();
#elif TESERRA_PLATFORM_LINUX
    std::raise(SIGTRAP);
#endif
}

[[noreturn]] void abortProcess() noexcept {
#if TESERRA_PLATFORM_WINDOWS
    // The failure is already logged. Skip the CRT's "abort() has been called" dialog, which would also
    // block unattended runs such as CI.
    _set_abort_behavior(0, _WRITE_ABORT_MSG);
#endif
    std::abort();
}

void defaultHandler(const AssertInfo& /*info*/) {
    if (isDebuggerAttached()) {
        breakIntoDebugger();
    }
    abortProcess();
}

constinit std::atomic<AssertHandler> currentHandler = &defaultHandler;

// Set while a failure is being reported on this thread. A second failure meanwhile (in a formatter, a
// log sink or the handler) aborts instead of recursing.
thread_local bool isReporting = false;

class ReportingScope {
public:
    ReportingScope() noexcept { isReporting = true; }
    ~ReportingScope() { isReporting = false; }

    ReportingScope(const ReportingScope&) = delete;
    ReportingScope& operator=(const ReportingScope&) = delete;
};

void report(AssertKind kind, std::string_view expression, std::source_location location,
            std::string_view format, std::format_args args) {
    if (isReporting) {
        abortProcess();
    }
    const ReportingScope scope;

    const std::string message = std::vformat(format, args);
    const AssertInfo info{kind, expression, message, location};
    // Bypasses the level filters: a failure is reported even if Core is turned off.
    log::detail::write(LogLevel::Critical, LogCategory::Core, location, "{}", describeAssert(info));
    log::flush();

    currentHandler.load(std::memory_order_acquire)(info);
}

} // namespace

AssertHandler setAssertHandler(AssertHandler handler) noexcept {
    return currentHandler.exchange(handler != nullptr ? handler : &defaultHandler, std::memory_order_acq_rel);
}

std::string describeAssert(const AssertInfo& info) {
    std::string text;
    switch (info.kind) {
        case AssertKind::Assert:
            text = std::format("Assertion failed: {} ", info.expression);
            break;
        case AssertKind::Verify:
            text = std::format("Verification failed: {} ", info.expression);
            break;
        case AssertKind::Unreachable:
            text = "Unreachable code reached ";
            break;
        case AssertKind::Fatal:
            text = "Fatal error ";
            break;
    }
    text += std::format("({}:{})", info.location.file_name(), info.location.line());
    if (!info.message.empty()) {
        text += ": ";
        text += info.message;
    }
    return text;
}

namespace detail {

void reportAssert(AssertKind kind, std::string_view expression, std::source_location location,
                  std::string_view format, std::format_args args) {
    report(kind, expression, location, format, args);
}

void reportFatal(AssertKind kind, std::source_location location, std::string_view format,
                 std::format_args args) {
    report(kind, {}, location, format, args);
    // Reached when the handler returns.
    abortProcess();
}

} // namespace detail

} // namespace teserra
