// The portable part of NativeFileSystem: reading, writing and checking files with std::filesystem and
// file streams. executableDirectory() and userDataDirectory() are in src/<os>/file_system_<os>.cpp.

#include <teserra/platform/file_system.hpp>

#include "common/file_system_paths.hpp"

#include <cerrno>
#include <cstdint>
#include <fstream>
#include <ios>

namespace teserra::platform {

namespace {

bool isValidUtf8(std::string_view text) noexcept {
    usize index = 0;
    while (index < text.size()) {
        const auto lead = static_cast<u8>(text[index]);
        if (lead < 0x80) {
            ++index;
            continue;
        }
        // The lead byte gives the length; a code point must use its shortest form (no overlong
        // encodings), and surrogates and values above U+10FFFF are not characters.
        usize length = 0;
        char32_t codepoint = 0;
        char32_t minimum = 0;
        if ((lead & 0xE0U) == 0xC0U) {
            length = 2;
            codepoint = lead & 0x1FU;
            minimum = 0x80;
        } else if ((lead & 0xF0U) == 0xE0U) {
            length = 3;
            codepoint = lead & 0x0FU;
            minimum = 0x800;
        } else if ((lead & 0xF8U) == 0xF0U) {
            length = 4;
            codepoint = lead & 0x07U;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (text.size() - index < length) {
            return false;
        }
        for (usize offset = 1; offset < length; ++offset) {
            const auto next = static_cast<u8>(text[index + offset]);
            if ((next & 0xC0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (next & 0x3FU);
        }
        const bool isSurrogate = codepoint >= 0xD800 && codepoint <= 0xDFFF;
        if (codepoint < minimum || codepoint > 0x10FFFF || isSurrogate) {
            return false;
        }
        index += length;
    }
    return true;
}

// The error left in errno by a file stream that failed to open.
std::error_code openError(int savedErrno) noexcept {
    return {savedErrno != 0 ? savedErrno : EIO, std::generic_category()};
}

} // namespace

Result<std::filesystem::path> toPath(std::string_view path) {
    if (path.empty()) {
        return fail(ErrorCode::InvalidArgument, Module::Platform, "The path is empty");
    }
    if (path.find('\0') != std::string_view::npos) {
        return fail(ErrorCode::InvalidArgument, Module::Platform, "The path '{}' contains a null character",
                    path.substr(0, path.find('\0')));
    }
    if (!isValidUtf8(path)) {
        return fail(ErrorCode::InvalidArgument, Module::Platform, "The path '{}' is not valid UTF-8", path);
    }
    const std::u8string_view text(reinterpret_cast<const char8_t*>(path.data()), path.size());
    return std::filesystem::path(text);
}

std::string toUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return {text.begin(), text.end()};
}

Result<void> checkAppName(std::string_view appName) {
    constexpr std::string_view kForbidden = "/\\:*?\"<>|";
    const bool hasForbidden = appName.find_first_of(kForbidden) != std::string_view::npos;
    bool hasControl = false;
    for (const char c : appName) {
        hasControl = hasControl || static_cast<u8>(c) < 0x20 || c == 0x7F;
    }
    // Windows drops a trailing dot or space from file names, so such a name would not round-trip.
    const bool badEnd = !appName.empty() && (appName.back() == '.' || appName.back() == ' ');
    if (appName.empty() || appName == "." || appName == ".." || hasForbidden || hasControl || badEnd ||
        !isValidUtf8(appName)) {
        return fail(ErrorCode::InvalidArgument, Module::Platform,
                    "'{}' cannot be an application directory name", appName);
    }
    return {};
}

Unexpected fileError(std::error_code error, std::string_view action, std::string_view path) {
    ErrorCode code = ErrorCode::IoFailure;
    if (error == std::errc::no_such_file_or_directory) {
        code = ErrorCode::NotFound;
    } else if (error == std::errc::permission_denied) {
        code = ErrorCode::AccessDenied;
    }
    // The generic condition's message is the C library's, not a localized system message in the ANSI code
    // page, when the system error has one.
    return fail(code, Module::Platform, "Cannot {} '{}': {}", action, path,
                error.default_error_condition().message());
}

Result<std::vector<std::byte>> NativeFileSystem::readFile(std::string_view path) {
    TESERRA_TRY_ASSIGN(const std::filesystem::path file, toPath(path));

    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(file, error);
    if (status.type() == std::filesystem::file_type::not_found) {
        return fail(ErrorCode::NotFound, Module::Platform, "File '{}' not found", path);
    }
    if (error) {
        return fileError(error, "read", path);
    }
    if (std::filesystem::is_directory(status)) {
        return fail(ErrorCode::InvalidArgument, Module::Platform, "Cannot read '{}': it is a directory",
                    path);
    }

    errno = 0;
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return fileError(openError(errno), "read", path);
    }
    const std::uintmax_t size = std::filesystem::file_size(file, error);
    if (error) {
        return fileError(error, "read", path);
    }
    std::vector<std::byte> bytes(static_cast<usize>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (static_cast<std::uintmax_t>(stream.gcount()) != size) {
        return fail(ErrorCode::IoFailure, Module::Platform, "Cannot read '{}': read {} of {} bytes", path,
                    stream.gcount(), size);
    }
    return bytes;
}

Result<void> NativeFileSystem::writeFile(std::string_view path, std::span<const std::byte> bytes) {
    TESERRA_TRY_ASSIGN(const std::filesystem::path file, toPath(path));

    std::error_code error;
    if (std::filesystem::is_directory(file, error)) {
        return fail(ErrorCode::InvalidArgument, Module::Platform, "Cannot write '{}': it is a directory",
                    path);
    }

    std::filesystem::path temporary = file;
    temporary += ".tmp";
    {
        errno = 0;
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return fileError(openError(errno), "write", path);
        }
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.close();
        if (stream.fail()) {
            std::filesystem::remove(temporary, error);
            return fail(ErrorCode::IoFailure, Module::Platform, "Cannot write {} bytes to '{}'", bytes.size(),
                        path);
        }
    }
    // Replaces the file in one step: readers see the old content or the new one, never a partial file.
    std::filesystem::rename(temporary, file, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return fileError(error, "write", path);
    }
    return {};
}

bool NativeFileSystem::exists(std::string_view path) {
    const Result<std::filesystem::path> file = toPath(path);
    std::error_code error;
    return file.has_value() && std::filesystem::exists(*file, error);
}

} // namespace teserra::platform
