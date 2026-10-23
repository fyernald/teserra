#pragma once

// The command line of teserra_shaderc:
//
//   teserra_shaderc <input.vert|.frag|.comp> -o <output.tshader> [-I <dir>]... [-D <name>[=<value>]]...
//                   [--diagnostics msvc|gcc]

#include <iosfwd>
#include <span>
#include <string_view>

namespace teserra::shaderc {

inline constexpr int kExitSuccess = 0;
inline constexpr int kExitShaderErrors = 1; ///< the shader has errors (compile or binding); they were printed
inline constexpr int kExitUsageError = 2;   ///< bad arguments, or a file could not be read or written

/// Runs the tool with `args`, without the program name. Help goes to `out`; diagnostics and usage errors
/// go to `err`. On success the .tshader blob is written to the `-o` file; on failure that file is removed, so
/// a stale one never looks up to date to the build. Returns the process exit code.
[[nodiscard]] int run(std::span<const std::string_view> args, std::ostream& out, std::ostream& err);

} // namespace teserra::shaderc
