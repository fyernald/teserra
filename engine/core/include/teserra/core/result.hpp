#pragma once

// Recoverable errors (ADR-0016): fallible functions return Result<T>, a value or an Error.
//
//   [[nodiscard]] Result<ImageData> decodeImage(std::span<const std::byte> bytes) {
//       if (bytes.empty()) {
//           return fail(ErrorCode::InvalidData, Module::Assets, "Image '{}' is empty", name);
//       }
//       TESERRA_TRY_ASSIGN(const Header header, readHeader(bytes)); // returns the error if it fails
//       TESERRA_TRY(checkSupported(header));                       // Result<void>
//       return ImageData{...};
//   }
//
// A Result must not be ignored: the type is [[nodiscard]], and fallible functions are marked
// [[nodiscard]] as well. Errors are logged once, where they are handled: TESERRA_LOG_ERROR(category,
// "Cannot load '{}': {}", path, error).

#include <teserra/core/types.hpp>

#include <tl/expected.hpp>

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace teserra {

/// What went wrong, independent of where. Stored and compared, never serialized.
enum class ErrorCode : u8 {
    InvalidArgument,   ///< An argument is outside what the function accepts (and is not a bug).
    NotFound,          ///< A file, asset or other named resource does not exist.
    AlreadyExists,     ///< Something with the same name or key exists already.
    AccessDenied,      ///< The operating system refused access.
    IoFailure,         ///< Reading or writing failed after the resource was found.
    InvalidData,       ///< Data is malformed, truncated or inconsistent.
    Unsupported,       ///< A valid request the engine or the hardware cannot fulfil.
    OutOfMemory,       ///< An allocation that the caller can recover from failed (e.g. GPU memory).
    CompilationFailed, ///< A shader or other program failed to compile or link.
    DeviceFailure,     ///< A device (GPU, audio, window system) could not be created or used.
    Internal,          ///< A third-party library failed in a way not covered above.
};

/// Engine module (or the game) that produced an error.
enum class Module : u8 {
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

/// Name of an error code, e.g. "NotFound".
[[nodiscard]] constexpr std::string_view errorCodeName(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::InvalidArgument:
            return "InvalidArgument";
        case ErrorCode::NotFound:
            return "NotFound";
        case ErrorCode::AlreadyExists:
            return "AlreadyExists";
        case ErrorCode::AccessDenied:
            return "AccessDenied";
        case ErrorCode::IoFailure:
            return "IoFailure";
        case ErrorCode::InvalidData:
            return "InvalidData";
        case ErrorCode::Unsupported:
            return "Unsupported";
        case ErrorCode::OutOfMemory:
            return "OutOfMemory";
        case ErrorCode::CompilationFailed:
            return "CompilationFailed";
        case ErrorCode::DeviceFailure:
            return "DeviceFailure";
        case ErrorCode::Internal:
            return "Internal";
    }
    return "Unknown";
}

/// Name of a module, e.g. "Platform".
[[nodiscard]] constexpr std::string_view moduleName(Module module) noexcept {
    switch (module) {
        case Module::Core:
            return "Core";
        case Module::Platform:
            return "Platform";
        case Module::Rhi:
            return "Rhi";
        case Module::Renderer:
            return "Renderer";
        case Module::Assets:
            return "Assets";
        case Module::Ecs:
            return "Ecs";
        case Module::Scene:
            return "Scene";
        case Module::App:
            return "App";
        case Module::Game:
            return "Game";
    }
    return "Unknown";
}

/// A recoverable failure. Formats as "<message> (<module>, <code>)", e.g.
/// "File 'hero.png' not found (Platform, NotFound)".
struct Error {
    ErrorCode code;
    Module module;
    /// Short description for people: what failed and on which object. May be empty.
    std::string message;

    friend bool operator==(const Error&, const Error&) = default;
};

// The one place that names the underlying implementation (ADR-0002, ADR-0016). Switching to
// std::expected when C++23 is adopted means replacing tl::expected and tl::unexpected here (and the
// include above); engine code only uses Result, Unexpected and fail().

/// A value of type T or an Error. Result<void> for operations without a value; `return {};` succeeds.
template <typename T>
using Result = tl::expected<T, Error>;

/// An Error on its way into a Result of any value type.
using Unexpected = tl::unexpected<Error>;

/// Makes the error for `return fail(...)` from a function that returns any Result.
template <typename... Args>
[[nodiscard]] Unexpected fail(ErrorCode code, Module module, std::format_string<Args...> format,
                              Args&&... args) {
    return Unexpected(Error{code, module, std::format(format, std::forward<Args>(args)...)});
}

} // namespace teserra

template <>
struct std::formatter<teserra::ErrorCode> : std::formatter<std::string_view> {
    auto format(teserra::ErrorCode code, std::format_context& context) const {
        return std::formatter<std::string_view>::format(teserra::errorCodeName(code), context);
    }
};

template <>
struct std::formatter<teserra::Module> : std::formatter<std::string_view> {
    auto format(teserra::Module module, std::format_context& context) const {
        return std::formatter<std::string_view>::format(teserra::moduleName(module), context);
    }
};

/// Formats an Error with "{}" (no format specification).
template <>
struct std::formatter<teserra::Error> {
    constexpr auto parse(std::format_parse_context& context) {
        // Only "{}". A specification is a compile error for checked format strings; this throw only
        // happens at runtime for unchecked ones (std::vformat), which engine code does not use.
        if (context.begin() != context.end() && *context.begin() != '}') {
            throw std::format_error("teserra::Error takes no format specification");
        }
        return context.begin();
    }

    auto format(const teserra::Error& error, std::format_context& context) const {
        if (error.message.empty()) {
            return std::format_to(context.out(), "{} ({})", error.code, error.module);
        }
        return std::format_to(context.out(), "{} ({}, {})", error.message, error.module, error.code);
    }
};

// Shared implementation of the TRY macros: a uniquely named variable per line holds the result.
#define TESERRA_DETAIL_CONCAT_IMPL(a, b) a##b
#define TESERRA_DETAIL_CONCAT(a, b) TESERRA_DETAIL_CONCAT_IMPL(a, b)
#define TESERRA_DETAIL_TRY_RESULT TESERRA_DETAIL_CONCAT(teserraTryResult, __LINE__)

/// TESERRA_TRY(expression): evaluates a Result and returns its error from the enclosing function, which
/// must return a Result. On success the value, if any, is discarded.
#define TESERRA_TRY(expression)                                                                              \
    do {                                                                                                     \
        auto&& teserraTryResult = (expression);                                                              \
        if (!teserraTryResult.has_value()) {                                                                 \
            return ::teserra::Unexpected(::std::move(teserraTryResult).error());                             \
        }                                                                                                    \
    } while (false)

/// TESERRA_TRY_ASSIGN(target, expression): like TESERRA_TRY, then moves the value into `target`, which
/// is a declaration (`const Image image`, `auto image`) or an existing variable. It expands to several
/// statements in the enclosing scope (the declaration must stay visible), so use it on a line of its
/// own, never as the body of an unbraced if or loop, and at most once per line.
// `target` cannot be parenthesized because it may be a declaration.
// NOLINTBEGIN(bugprone-macro-parentheses)
#define TESERRA_TRY_ASSIGN(target, expression)                                                               \
    auto&& TESERRA_DETAIL_TRY_RESULT = (expression);                                                         \
    if (!TESERRA_DETAIL_TRY_RESULT.has_value()) {                                                            \
        return ::teserra::Unexpected(::std::move(TESERRA_DETAIL_TRY_RESULT).error());                        \
    }                                                                                                        \
    target = *::std::move(TESERRA_DETAIL_TRY_RESULT)
// NOLINTEND(bugprone-macro-parentheses)
