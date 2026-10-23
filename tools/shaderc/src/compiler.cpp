#include "compiler.hpp"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <algorithm>
#include <array>
#include <deque>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <string_view>
#include <system_error>

namespace teserra::shaderc {

namespace {

namespace fs = std::filesystem;

constexpr int kGlslVersion = 450;
constexpr int kVulkanClientVersion = 100; // the version of the Vulkan GLSL rules, not of Vulkan

// glslang's process-wide tables. They are never finalized: the tool and the tests compile until exit.
void initializeGlslang() {
    static const bool kInitialized = glslang::InitializeProcess();
    static_cast<void>(kInitialized);
}

// Paths travel through glslang as UTF-8 strings: the names it prints in messages and passes back to the
// includer.
std::string toUtf8(const fs::path& path) {
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

fs::path fromUtf8(std::string_view text) {
    return {std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size())};
}

fs::path absoluteNormal(const fs::path& path) {
    std::error_code error;
    const fs::path absolute = fs::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

std::optional<std::string> readFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream text;
    text << file.rdbuf();
    return std::move(text).str();
}

// #include "x" relative to the including file, then (by glslang's fallback to includeSystem) and for
// #include <x>, in the include directories. Records every file it opens. It owns the texts and results it
// returns until it is destroyed, after glslang has parsed the shader.
class Includer final : public glslang::TShader::Includer {
public:
    Includer(const std::vector<fs::path>& includeDirectories, std::vector<fs::path>& includedFiles)
        : m_includeDirectories(includeDirectories), m_includedFiles(includedFiles) {}

    IncludeResult* includeLocal(const char* headerName, const char* includerName,
                                size_t /*inclusionDepth*/) override {
        return open(fromUtf8(includerName).parent_path() / fromUtf8(headerName));
    }

    IncludeResult* includeSystem(const char* headerName, const char* /*includerName*/,
                                 size_t /*inclusionDepth*/) override {
        for (const fs::path& directory : m_includeDirectories) {
            if (IncludeResult* result = open(directory / fromUtf8(headerName))) {
                return result;
            }
        }
        return nullptr;
    }

    void releaseInclude(IncludeResult* /*result*/) override {}

private:
    IncludeResult* open(const fs::path& candidate) {
        std::error_code error;
        if (!fs::is_regular_file(candidate, error)) {
            return nullptr;
        }
        std::optional<std::string> text = readFile(candidate);
        if (!text) {
            return nullptr;
        }
        const fs::path path = absoluteNormal(candidate);
        if (std::ranges::find(m_includedFiles, path) == m_includedFiles.end()) {
            m_includedFiles.push_back(path);
        }
        const std::string& source = m_texts.emplace_back(std::move(*text));
        return m_results
            .emplace_back(
                std::make_unique<IncludeResult>(toUtf8(path), source.data(), source.size(), nullptr))
            .get();
    }

    const std::vector<fs::path>& m_includeDirectories;
    std::vector<fs::path>& m_includedFiles;
    std::deque<std::string> m_texts; // a deque: results point into texts that must not move
    std::vector<std::unique_ptr<IncludeResult>> m_results;
};

// The glslang stage of a single ShaderStage bit.
std::optional<EShLanguage> glslangStage(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:
            return EShLangVertex;
        case ShaderStage::Fragment:
            return EShLangFragment;
        case ShaderStage::Compute:
            return EShLangCompute;
        default:
            return std::nullopt;
    }
}

std::string_view stageMacro(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:
            return "TESERRA_STAGE_VERTEX";
        case ShaderStage::Fragment:
            return "TESERRA_STAGE_FRAGMENT";
        default:
            return "TESERRA_STAGE_COMPUTE";
    }
}

// Text glslang reads before the source. It does not count for the source's line numbers.
std::string preamble(ShaderStage stage, const std::vector<std::pair<std::string, std::string>>& defines) {
    std::string text = "#extension GL_GOOGLE_include_directive : enable\n#define TESERRA_SHADER 1\n";
    text += "#define ";
    text += stageMacro(stage);
    text += " 1\n";
    for (const auto& [name, value] : defines) {
        text += "#define " + name + ' ' + (value.empty() ? std::string("1") : value) + '\n';
    }
    return text;
}

// Lines that only repeat that errors happened.
bool isNoise(std::string_view message) {
    static const std::regex kSummary(R"(\d+ compilation errors?\.\s+No code generated\.)");
    return message == "'' : compilation terminated" ||
           std::regex_match(message.begin(), message.end(), kSummary);
}

// Parses glslang's info log: "ERROR: <file>:<line>: <message>", "WARNING: ...", and messages without a
// location, which are about `input`.
void appendDiagnostics(std::string_view log, const fs::path& input, std::vector<Diagnostic>& diagnostics) {
    static const std::regex kLocated(R"(^(.+?):(\d+): (.*)$)");
    std::istringstream lines{std::string(log)};
    std::string line;
    while (std::getline(lines, line)) {
        // glslang ends its messages with a space.
        while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        Diagnostic diagnostic{.severity = Severity::Error, .file = input};
        std::string_view text = line;
        if (text.starts_with("ERROR: ")) {
            text.remove_prefix(7);
        } else if (text.starts_with("WARNING: ")) {
            diagnostic.severity = Severity::Warning;
            text.remove_prefix(9);
        }
        std::match_results<std::string_view::const_iterator> match;
        if (std::regex_match(text.begin(), text.end(), match, kLocated)) {
            diagnostic.file = fromUtf8(std::string_view(match[1].first, match[1].second)).lexically_normal();
            diagnostic.line = static_cast<std::uint32_t>(std::stoul(match[2].str()));
            text = std::string_view(match[3].first, match[3].second);
        }
        if (!isNoise(text)) {
            diagnostic.message = std::string(text);
            diagnostics.push_back(std::move(diagnostic));
        }
    }
}

} // namespace

std::optional<ShaderStage> stageFromPath(const std::filesystem::path& path) {
    const fs::path extension = path.extension();
    if (extension == ".vert") {
        return ShaderStage::Vertex;
    }
    if (extension == ".frag") {
        return ShaderStage::Fragment;
    }
    if (extension == ".comp") {
        return ShaderStage::Compute;
    }
    return std::nullopt;
}

CompileResult compileGlsl(const std::filesystem::path& input, ShaderStage stage,
                          const CompileOptions& options) {
    initializeGlslang();
    CompileResult result;
    const fs::path path = absoluteNormal(input);
    const std::optional<EShLanguage> glslangLanguage = glslangStage(stage);
    if (!glslangLanguage) {
        result.diagnostics.push_back({.severity = Severity::Error,
                                      .file = path,
                                      .message = "the stage must be one of vertex, fragment or compute"});
        return result;
    }
    const std::optional<std::string> source = readFile(path);
    if (!source) {
        result.diagnostics.push_back(
            {.severity = Severity::Error, .file = path, .message = "cannot read the file"});
        return result;
    }

    const EShLanguage language = *glslangLanguage;
    const std::string name = toUtf8(path);
    const std::string preambleText = preamble(stage, options.defines);
    const char* text = source->c_str();
    const int length = static_cast<int>(source->size());
    const std::array<const char*, 1> names{name.c_str()};

    glslang::TShader shader(language);
    shader.setStringsWithLengthsAndNames(&text, &length, names.data(), 1);
    shader.setPreamble(preambleText.c_str());
    shader.setEnvInput(glslang::EShSourceGlsl, language, glslang::EShClientVulkan, kVulkanClientVersion);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);

    const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    Includer includer(options.includeDirectories, result.includedFiles);
    const bool parsed = shader.parse(GetDefaultResources(), kGlslVersion, false, messages, includer);
    appendDiagnostics(shader.getInfoLog(), path, result.diagnostics);
    if (!parsed) {
        return result;
    }

    glslang::TProgram program;
    program.addShader(&shader);
    const bool linked = program.link(messages);
    appendDiagnostics(program.getInfoLog(), path, result.diagnostics);
    if (!linked) {
        return result;
    }

    std::vector<unsigned int> words;
    spv::SpvBuildLogger logger;
    glslang::SpvOptions spvOptions;
    glslang::GlslangToSpv(*program.getIntermediate(language), words, &logger, &spvOptions);
    // The SPIR-V generator only reports limitations of glslang itself; they do not fail the shader.
    std::istringstream generatorMessages{logger.getAllMessages()};
    for (std::string line; std::getline(generatorMessages, line);) {
        if (!line.empty()) {
            result.diagnostics.push_back({.severity = Severity::Warning, .file = path, .message = line});
        }
    }
    result.spirv.assign(words.begin(), words.end());
    return result;
}

DiagnosticFormat nativeDiagnosticFormat() noexcept {
#if defined(_WIN32)
    return DiagnosticFormat::Msvc;
#else
    return DiagnosticFormat::Gcc;
#endif
}

std::string formatDiagnostic(const Diagnostic& diagnostic, DiagnosticFormat format) {
    const std::u8string file = fs::path(diagnostic.file).make_preferred().u8string();
    std::string text(reinterpret_cast<const char*>(file.data()), file.size());
    if (diagnostic.line != 0) {
        const std::string line = std::to_string(diagnostic.line);
        text += format == DiagnosticFormat::Msvc ? '(' + line + ')' : ':' + line;
    }
    text += diagnostic.severity == Severity::Error ? ": error: " : ": warning: ";
    text += diagnostic.message;
    return text;
}

} // namespace teserra::shaderc
