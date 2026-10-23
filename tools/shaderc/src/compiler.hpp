#pragma once

// The GLSL to SPIR-V compiler of teserra_shaderc (ADR-0017, step 1). Shaders are GLSL 450 in the Vulkan
// dialect, one file per stage; glslang compiles them for Vulkan 1.2 (SPIR-V 1.5).

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace teserra::shaderc {

enum class ShaderStage : std::uint8_t {
    Vertex,
    Fragment,
    Compute
};

/// The stage of a shader file from its extension: `.vert`, `.frag` or `.comp`; `std::nullopt` otherwise.
[[nodiscard]] std::optional<ShaderStage> stageFromPath(const std::filesystem::path& path);

struct CompileOptions {
    /// Searched for `#include "x"` after the including file's directory, and for `#include <x>`.
    std::vector<std::filesystem::path> includeDirectories{};
    /// Macros defined before the source, as `#define name value` (an empty value defines it as 1).
    std::vector<std::pair<std::string, std::string>> defines{};
};

enum class Severity : std::uint8_t {
    Error,
    Warning
};

struct Diagnostic {
    Severity severity = Severity::Error;
    /// The absolute path of the file the message is about: the input or an included file.
    std::filesystem::path file{};
    /// 1-based line in `file`; 0 when the message has no line (link errors, unreadable files).
    std::uint32_t line = 0;
    std::string message{};
};

struct CompileResult {
    /// The SPIR-V module; empty when compilation failed.
    std::vector<std::uint32_t> spirv{};
    /// Errors and warnings in source order.
    std::vector<Diagnostic> diagnostics{};
    /// Absolute paths of every file the shader included, each once, for build dependency tracking.
    std::vector<std::filesystem::path> includedFiles{};

    [[nodiscard]] bool succeeded() const noexcept { return !spirv.empty(); }
};

/// Compiles the GLSL file `input` as `stage`. Besides the `defines`, the source sees `TESERRA_SHADER` and
/// one of `TESERRA_STAGE_VERTEX`, `TESERRA_STAGE_FRAGMENT` or `TESERRA_STAGE_COMPUTE` defined to 1, and
/// `#include` works without an `#extension` line. Diagnostics name files by absolute path.
[[nodiscard]] CompileResult compileGlsl(const std::filesystem::path& input, ShaderStage stage,
                                        const CompileOptions& options);

enum class DiagnosticFormat : std::uint8_t {
    Msvc, ///< `file(line): error: message`, clickable in Visual Studio
    Gcc,  ///< `file:line: error: message`, clickable in most other IDEs and terminals
};

/// The format IDEs on this platform expect: `Msvc` on Windows, `Gcc` elsewhere.
[[nodiscard]] DiagnosticFormat nativeDiagnosticFormat() noexcept;

/// One line for a compiler-style error list, without a trailing newline.
[[nodiscard]] std::string formatDiagnostic(const Diagnostic& diagnostic, DiagnosticFormat format);

} // namespace teserra::shaderc
