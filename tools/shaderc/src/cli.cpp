#include "cli.hpp"

#include <teserra/rhi/shader_blob.hpp>

#include "compiler.hpp"
#include "translator.hpp"

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

constexpr std::string_view kUsage = "usage: teserra_shaderc <input.vert|.frag|.comp> -o <output.tshader>\n"
                                    "                       [-I <dir>]... [-D <name>[=<value>]]... "
                                    "[--depfile <file>] [--diagnostics msvc|gcc]\n";

struct Arguments {
    std::optional<fs::path> input;
    std::optional<fs::path> output;
    std::optional<fs::path> depfile;
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
        } else if (arg == "--depfile" || arg.starts_with("--depfile=")) {
            const std::optional<std::string_view> path = value(arg == "--depfile" ? arg : "--depfile=");
            if (!path || path->empty()) {
                return "--depfile needs a file";
            }
            parsed.depfile = pathFromArgument(*path);
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

// A path in Make syntax, as Ninja and CMake read depfiles: absolute, forward slashes, and spaces, '#' and
// '$' escaped.
std::string makePath(const fs::path& path) {
    const std::u8string generic = fs::absolute(path).lexically_normal().generic_u8string();
    std::string escaped;
    for (const char8_t c : generic) {
        if (c == u8' ' || c == u8'#') {
            escaped += '\\';
        } else if (c == u8'$') {
            escaped += '$';
        }
        escaped += static_cast<char>(c);
    }
    return escaped;
}

// `<output>: <input> <included files>`, so the build reruns the tool when any of them changes.
std::string depfileText(const fs::path& output, const fs::path& input,
                        const std::vector<fs::path>& included) {
    std::string text = makePath(output) + ": " + makePath(input);
    for (const fs::path& file : included) {
        text += " \\\n  " + makePath(file);
    }
    return text + '\n';
}

int usageError(std::ostream& err, std::string_view message) {
    err << "teserra_shaderc: error: " << message << '\n' << kUsage;
    return kExitUsageError;
}

bool writeFile(const fs::path& path, std::span<const std::byte> bytes) {
    std::error_code error;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), error);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
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
    fs::remove(output, ignored); // from here on, every failure leaves no output and no depfile
    if (arguments.depfile) {
        fs::remove(*arguments.depfile, ignored);
    }

    const std::optional<ShaderStage> stage = stageFromPath(*arguments.input);
    if (!stage) {
        return usageError(err, "cannot tell the shader stage of '" + pathText(*arguments.input) +
                                   "': the extension must be .vert, .frag or .comp");
    }

    const auto print = [&](const std::vector<Diagnostic>& diagnostics) {
        for (const Diagnostic& diagnostic : diagnostics) {
            err << formatDiagnostic(diagnostic, arguments.format) << '\n';
        }
    };
    const CompileResult compiled = compileGlsl(*arguments.input, *stage, arguments.options);
    print(compiled.diagnostics);
    if (!compiled.succeeded()) {
        return kExitShaderErrors;
    }
    const Translation translation =
        translateForOpenGl(compiled.spirv, *stage, fs::absolute(*arguments.input).lexically_normal());
    print(translation.diagnostics);
    if (!translation.succeeded()) {
        return kExitShaderErrors;
    }

    const std::vector<std::byte> blob =
        rhi::writeShaderBlob({.stage = *stage,
                              .spirv = std::as_bytes(std::span(compiled.spirv)),
                              .glsl = translation.glsl,
                              .reflection = translation.reflection});
    if (!writeFile(output, blob)) {
        fs::remove(output, ignored);
        err << "teserra_shaderc: error: cannot write '" << pathText(output) << "'\n";
        return kExitUsageError;
    }
    if (arguments.depfile) {
        const std::string text = depfileText(output, *arguments.input, compiled.includedFiles);
        if (!writeFile(*arguments.depfile, std::as_bytes(std::span(text)))) {
            fs::remove(output, ignored);
            fs::remove(*arguments.depfile, ignored);
            err << "teserra_shaderc: error: cannot write '" << pathText(*arguments.depfile) << "'\n";
            return kExitUsageError;
        }
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
