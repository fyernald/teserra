// The Windows part of NativeFileSystem: the directories that only the OS knows. Wide strings stay in this
// file; the API sees UTF-8 (ADR-0015).

#include <teserra/platform/file_system.hpp>

#include "common/file_system_paths.hpp"

#define WIN32_LEAN_AND_MEAN // NOLINT(readability-identifier-naming): Windows SDK configuration macro
#define NOMINMAX            // NOLINT(readability-identifier-naming): Windows SDK configuration macro
#include <shlobj.h>
#include <windows.h>

#include <memory>
#include <string>

namespace teserra::platform {

namespace {

// The longest path Windows can return, with long paths enabled.
constexpr DWORD kMaxPathLength = 32768;

struct CoTaskMemDeleter {
    void operator()(wchar_t* memory) const noexcept { CoTaskMemFree(memory); }
};

} // namespace

Result<std::string> NativeFileSystem::executableDirectory() {
    // GetModuleFileNameW truncates silently when the buffer is too small, so it grows until the path fits.
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const DWORD size = static_cast<DWORD>(buffer.size());
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), size);
        if (length == 0) {
            return fail(ErrorCode::IoFailure, Module::Platform,
                        "Cannot get the executable's path: Windows error {}", GetLastError());
        }
        if (length < size) {
            buffer.resize(length);
            break;
        }
        if (size >= kMaxPathLength) {
            return fail(ErrorCode::IoFailure, Module::Platform,
                        "Cannot get the executable's path: longer than {} characters", kMaxPathLength);
        }
        buffer.resize(static_cast<usize>(size) * 2);
    }
    return toUtf8(std::filesystem::path(buffer).parent_path());
}

Result<std::string> NativeFileSystem::userDataDirectory(std::string_view appName) {
    TESERRA_TRY(checkAppName(appName));
    TESERRA_TRY_ASSIGN(const std::filesystem::path name, toPath(appName));

    // The folder must be freed even when the call fails.
    wchar_t* folder = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &folder);
    const std::unique_ptr<wchar_t, CoTaskMemDeleter> ownedFolder(folder);
    if (FAILED(result)) {
        return fail(ErrorCode::IoFailure, Module::Platform,
                    "Cannot find the user's application data folder for '{}': HRESULT 0x{:08X}", appName,
                    static_cast<u32>(result));
    }

    const std::filesystem::path directory = std::filesystem::path(ownedFolder.get()) / name;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return fileError(error, "create the directory", toUtf8(directory));
    }
    return toUtf8(directory);
}

} // namespace teserra::platform
