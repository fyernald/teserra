#pragma once

// Assertions for programmer errors (ADR-0016). Recoverable failures return Result instead.
//
//   TESERRA_ASSERT(index < size, "Index {} out of range for '{}'", index, name);
//   TESERRA_VERIFY(glUnmapNamedBuffer(id) == GL_TRUE);   // the call happens in every build
//   default: TESERRA_UNREACHABLE();
//   TESERRA_FATAL("GPU device lost: {}", reason);
//
// The message is optional; when given it is a std::format string literal checked at compile time. A
// condition that contains top-level commas needs parentheses: TESERRA_ASSERT((std::is_same_v<A, B>)).
//
// A failure is logged at Critical (category Core) with the caller's location, the log is flushed, and
// the assert handler runs. The default handler breaks into an attached debugger and aborts. Functions
// whose preconditions are asserted have narrow contracts and are not noexcept, so that a test handler
// can report a failure by throwing.

#include <teserra/core/config.hpp>
#include <teserra/core/types.hpp>

#include <format>
#include <source_location>
#include <string>
#include <string_view>

// TESERRA_ENABLE_ASSERTS: 1 if TESERRA_ASSERT is checked. Set by the TESERRA_ENABLE_ASSERTS CMake option
// (default: Debug and RelWithDebInfo); without CMake it follows TESERRA_DEBUG_BUILD.
#if !defined(TESERRA_ENABLE_ASSERTS)
#define TESERRA_ENABLE_ASSERTS TESERRA_DEBUG_BUILD
#endif

namespace teserra {

/// Which macro reported a failure.
enum class AssertKind : u8 {
    Assert,      ///< TESERRA_ASSERT
    Verify,      ///< TESERRA_VERIFY
    Unreachable, ///< TESERRA_UNREACHABLE
    Fatal,       ///< TESERRA_FATAL
};

/// A failed assertion as passed to the assert handler. The views are only valid during the call.
struct AssertInfo {
    AssertKind kind;
    /// Source text of the condition; empty for TESERRA_UNREACHABLE and TESERRA_FATAL.
    std::string_view expression;
    /// Formatted message; empty if none was given.
    std::string_view message;
    std::source_location location;
};

/// Called after a failure has been logged. If it returns, TESERRA_ASSERT and TESERRA_VERIFY continue
/// after the check, while TESERRA_UNREACHABLE and TESERRA_FATAL abort. It may throw (tests do), but not
/// through noexcept frames.
using AssertHandler = void (*)(const AssertInfo& info);

/// Installs `handler` for all threads and returns the previous one; nullptr restores the default
/// handler, which breaks into an attached debugger and aborts. Thread-safe.
AssertHandler setAssertHandler(AssertHandler handler) noexcept;

/// The standard one-line description of a failure, e.g.
/// "Assertion failed: index < size (buffer.cpp:42): Index 7 out of range for 'quad'".
[[nodiscard]] std::string describeAssert(const AssertInfo& info);

namespace detail {

void reportAssert(AssertKind kind, std::string_view expression, std::source_location location,
                  std::string_view format, std::format_args args);
[[noreturn]] void reportFatal(AssertKind kind, std::source_location location, std::string_view format,
                              std::format_args args);

template <typename... Args>
void assertFailed(AssertKind kind, std::string_view expression, std::source_location location,
                  std::format_string<Args...> format, Args&&... args) {
    reportAssert(kind, expression, location, format.get(), std::make_format_args(args...));
}

template <typename... Args>
[[noreturn]] void fatal(AssertKind kind, std::source_location location, std::format_string<Args...> format,
                        Args&&... args) {
    reportFatal(kind, location, format.get(), std::make_format_args(args...));
}

} // namespace detail

} // namespace teserra

// The optional message is appended to an empty string literal, so `"" __VA_ARGS__` is either "" or the
// caller's format string followed by its arguments. This works without __VA_OPT__, which MSVC's
// traditional preprocessor lacks.
// `text` is the condition as written, stringified by the public macro before macros in it expand.
#define TESERRA_DETAIL_ASSERT_CHECK(assertKind, condition, text, ...)                                        \
    do {                                                                                                     \
        if (!(condition)) [[unlikely]] {                                                                     \
            ::teserra::detail::assertFailed((assertKind), text, ::std::source_location::current(),           \
                                            "" __VA_ARGS__);                                                 \
        }                                                                                                    \
    } while (false)

#if TESERRA_ENABLE_ASSERTS
/// TESERRA_ASSERT(condition[, format, args...]): checks a precondition or invariant. Compiled out
/// (condition and arguments not evaluated) when TESERRA_ENABLE_ASSERTS is 0.
#define TESERRA_ASSERT(condition, ...)                                                                       \
    TESERRA_DETAIL_ASSERT_CHECK(::teserra::AssertKind::Assert, condition, #condition, __VA_ARGS__)
/// TESERRA_VERIFY(condition[, format, args...]): like TESERRA_ASSERT, but the condition is evaluated in
/// every build; only the check is compiled out.
#define TESERRA_VERIFY(condition, ...)                                                                       \
    TESERRA_DETAIL_ASSERT_CHECK(::teserra::AssertKind::Verify, condition, #condition, __VA_ARGS__)
#else
// sizeof keeps the condition compiled (and its variables used) without evaluating it.
#define TESERRA_ASSERT(condition, ...) static_cast<void>(sizeof(!(condition)))
#define TESERRA_VERIFY(condition, ...) static_cast<void>(condition)
#endif

/// TESERRA_UNREACHABLE([format, args...]): marks code that cannot run. Always active; aborts.
#define TESERRA_UNREACHABLE(...)                                                                             \
    ::teserra::detail::fatal(::teserra::AssertKind::Unreachable, ::std::source_location::current(),          \
                             "" __VA_ARGS__)

/// TESERRA_FATAL(format, args...): unrecoverable condition. Always active; logs, flushes and aborts.
#define TESERRA_FATAL(...)                                                                                   \
    ::teserra::detail::fatal(::teserra::AssertKind::Fatal, ::std::source_location::current(), __VA_ARGS__)
