#include "cli.hpp"

#include "compiler.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ostream>
#include <string>
#include <system_error>

namespace teserra::shaderc {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kUsage =
    "usage: teserra_shaderc <input.vert|.frag|.comp> -o <output.spv> [-I <dir>]... [-D <name>[=<value>]]...\n"
    "                       [--diagnostics msvc|gcc]\n";

struct Arguments {
    std::optional<fs::path> input;
    std::optional<fs::path> output;
    CompileOptions options;
    DiagnosticFormat format = nativeDiagnosticFormat();
    bool help = false;
};

fs::path pathFromArgument(std::string_view argument) {
    return {std::u8string(reinterpret_cast<const char8_t*>(argument.data()), argument.size())};
}

std::string pathText(const fs::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

// Returns an error message, or nothing when the arguments are complete.
std::optional<std::string> parse(std::span<const std::string_view> args, Arguments& parsed) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        // The value of `-o x` / `-Ix` style options: attached, or the next argument.
        const auto value = [&](std::string_view option) -> std::optional<std::string_view> {
            if (arg.size() > option.size()) {
                return arg.substr(option.size());
            }
            if (i + 1 < args.size()) {
                return args[++i];
            }
            return std::nullopt;
        };

        if (arg == "-h" || arg == "--help") {
            parsed.help = true;
        } else if (arg.starts_with("-o")) {
            const std::optional<std::string_view> path = value("-o");
            if (!path) {
                return "-o needs an output file";
            }
            parsed.output = pathFromArgument(*path);
        } else if (arg.starts_with("-I")) {
            const std::optional<std::string_view> directory = value("-I");
            if (!directory) {
                return "-I needs a directory";
            }
            parsed.options.includeDirectories.push_back(pathFromArgument(*directory));
        } else if (arg.starts_with("-D")) {
            const std::optional<std::string_view> define = value("-D");
            if (!define || define->empty() || define->front() == '=') {
                return "-D needs a macro name";
            }
            const std::size_t equals = define->find('=');
            parsed.options.defines.emplace_back(
                std::string(define->substr(0, equals)),
                equals == std::string_view::npos ? std::string() : std::string(define->substr(equals + 1)));
        } else if (arg == "--diagnostics" || arg.starts_with("--diagnostics=")) {
            const std::optional<std::string_view> format =
                value(arg == "--diagnostics" ? arg : "--diagnostics=");
            if (format == "msvc") {
                parsed.format = DiagnosticFormat::Msvc;
            } else if (format == "gcc") {
                parsed.format = DiagnosticFormat::Gcc;
            } else {
                return "--diagnostics is msvc or gcc";
            }
        } else if (arg.starts_with('-') && arg != "-") {
            return "unknown option '" + std::string(arg) + "'";
        } else if (parsed.input) {
            return "more than one input file ('" + std::string(arg) + "')";
        } else {
            parsed.input = pathFromArgument(arg);
        }
    }
    if (parsed.help) {
        return std::nullopt;
    }
    if (!parsed.input) {
        return "no input file";
    }
    if (!parsed.output) {
        return "no output file (-o)";
    }
    return std::nullopt;
}

int usageError(std::ostream& err, std::string_view message) {
    err << "teserra_shaderc: error: " << message << '\n' << kUsage;
    return kExitUsageError;
}

bool writeSpirv(const fs::path& path, const std::vector<std::uint32_t>& spirv) {
    std::error_code error;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), error);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(spirv.data()),
               static_cast<std::streamsize>(spirv.size() * sizeof(std::uint32_t)));
    file.close();
    return !file.fail();
}

int runChecked(std::span<const std::string_view> args, std::ostream& out, std::ostream& err) {
    Arguments arguments;
    if (const std::optional<std::string> problem = parse(args, arguments)) {
        return usageError(err, *problem);
    }
    if (arguments.help) {
        out << kUsage;
        return kExitSuccess;
    }

    const fs::path& output = *arguments.output;
    std::error_code ignored;
    fs::remove(output, ignored); // from here on, every failure leaves no output

    const std::optional<ShaderStage> stage = stageFromPath(*arguments.input);
    if (!stage) {
        return usageError(err, "cannot tell the shader stage of '" + pathText(*arguments.input) +
                                   "': the extension must be .vert, .frag or .comp");
    }

    const CompileResult result = compileGlsl(*arguments.input, *stage, arguments.options);
    for (const Diagnostic& diagnostic : result.diagnostics) {
        err << formatDiagnostic(diagnostic, arguments.format) << '\n';
    }
    if (!result.succeeded()) {
        return kExitShaderErrors;
    }
    if (!writeSpirv(output, result.spirv)) {
        fs::remove(output, ignored);
        err << "teserra_shaderc: error: cannot write '" << pathText(output) << "'\n";
        return kExitUsageError;
    }
    return kExitSuccess;
}

} // namespace

int run(std::span<const std::string_view> args, std::ostream& out, std::ostream& err) {
    try {
        return runChecked(args, out, err);
    } catch (const std::exception& exception) {
        err << "teserra_shaderc: error: " << exception.what() << '\n';
        return kExitUsageError;
    }
}

} // namespace teserra::shaderc
