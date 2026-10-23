// The GLSL to SPIR-V compiler of teserra_shaderc (ADR-0017), on the fixtures in shaders/.

#include "compiler.hpp"

#include "spirv_validation.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

using namespace teserra::shaderc;
using Catch::Matchers::ContainsSubstring;

namespace {

namespace fs = std::filesystem;

constexpr std::uint32_t kSpirvMagic = 0x07230203;
constexpr std::uint32_t kSpirv15 = 0x00010500;

fs::path fixture(std::string_view name) {
    return (fs::path(TESERRA_SHADERC_FIXTURES) / name).lexically_normal();
}

CompileResult compileFixture(std::string_view name, const CompileOptions& options = {}) {
    const fs::path path = fixture(name);
    const std::optional<ShaderStage> stage = stageFromPath(path);
    REQUIRE(stage.has_value());
    return compileGlsl(path, stage.value_or(ShaderStage::Vertex), options);
}

CompileOptions withIncludeDirectory() {
    return {.includeDirectories = {fixture("include_dir")}};
}

} // namespace

TEST_CASE("shaderc: the stage comes from the file extension", "[shaderc][compiler]") {
    CHECK(stageFromPath("sprite.vert") == ShaderStage::Vertex);
    CHECK(stageFromPath("dir/sprite.frag") == ShaderStage::Fragment);
    CHECK(stageFromPath("cull.comp") == ShaderStage::Compute);
    CHECK_FALSE(stageFromPath("frame.glsl").has_value());
    CHECK_FALSE(stageFromPath("sprite.vert.spv").has_value());
    CHECK_FALSE(stageFromPath("vert").has_value());
}

TEST_CASE("shaderc: vertex, fragment and compute shaders compile to SPIR-V 1.5", "[shaderc][compiler]") {
    for (const std::string_view name : {"triangle.vert", "triangle.frag", "fill.comp"}) {
        INFO(name);
        const CompileResult result = compileFixture(name, withIncludeDirectory());

        CHECK(result.diagnostics.empty());
        REQUIRE(result.succeeded());
        REQUIRE(result.spirv.size() > 5);
        CHECK(result.spirv[0] == kSpirvMagic);
        CHECK(result.spirv[1] == kSpirv15);
    }
}

TEST_CASE("shaderc: the SPIR-V passes spirv-val", "[shaderc][compiler][validation]") {
    if (!test::spirvValidationAvailable()) {
        SKIP("SPIR-V validation needs TESERRA_SHADER_VALIDATION (vcpkg feature shader-validation)");
    }
    for (const std::string_view name : {"triangle.vert", "triangle.frag", "fill.comp", "macros.vert"}) {
        INFO(name);
        const CompileResult result =
            compileFixture(name, {.includeDirectories = {fixture("include_dir")},
                                  .defines = {{"TESERRA_TEST_FLAG", ""}, {"TESERRA_TEST_VALUE", "42"}}});
        REQUIRE(result.succeeded());
        CHECK(test::validateSpirv(result.spirv).empty());
    }

    // The validator is not a formality: a module cut in the middle of an instruction is rejected.
    std::vector<std::uint32_t> broken = compileFixture("fill.comp").spirv;
    REQUIRE(broken.size() > 10);
    broken.resize(broken.size() - 1);
    CHECK_FALSE(test::validateSpirv(broken).empty());
}

TEST_CASE("shaderc: #include works without an #extension line, relative to the including file",
          "[shaderc][compiler][include]") {
    const CompileResult result = compileFixture("triangle.vert");

    REQUIRE(result.succeeded());
    // common/frame.glsl includes "constants.glsl" next to itself, not next to triangle.vert.
    CHECK(result.includedFiles ==
          std::vector<fs::path>{fixture("common/frame.glsl"), fixture("common/constants.glsl")});
}

TEST_CASE("shaderc: include directories serve #include <x> and #include \"x\" not found nearby",
          "[shaderc][compiler][include]") {
    const CompileResult angled = compileFixture("triangle.frag", withIncludeDirectory());
    REQUIRE(angled.succeeded());
    CHECK(angled.includedFiles == std::vector<fs::path>{fixture("include_dir/teserra/colors.glsl")});

    const CompileResult quoted = compileFixture("quoted_from_include_dir.frag", withIncludeDirectory());
    REQUIRE(quoted.succeeded());
    CHECK(quoted.includedFiles == std::vector<fs::path>{fixture("include_dir/teserra/colors.glsl")});

    const CompileResult missing = compileFixture("triangle.frag");
    CHECK_FALSE(missing.succeeded());
    REQUIRE_FALSE(missing.diagnostics.empty());
    CHECK(missing.diagnostics[0].file == fixture("triangle.frag"));
    CHECK(missing.diagnostics[0].line == 3);
}

TEST_CASE("shaderc: a syntax error gives one diagnostic with the file and line",
          "[shaderc][compiler][errors]") {
    const CompileResult result = compileFixture("errors/syntax_error.frag");

    CHECK_FALSE(result.succeeded());
    CHECK(result.spirv.empty());
    REQUIRE(result.diagnostics.size() == 1);
    const Diagnostic& error = result.diagnostics[0];
    CHECK(error.severity == Severity::Error);
    CHECK(error.file == fixture("errors/syntax_error.frag"));
    CHECK(error.line == 7); // the preamble does not shift line numbers
    CHECK(error.message == "'undeclaredColor' : undeclared identifier");
}

TEST_CASE("shaderc: an error in an included file names that file and its line",
          "[shaderc][compiler][errors]") {
    const CompileResult result = compileFixture("errors/include_error.frag");

    CHECK_FALSE(result.succeeded());
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics[0].file == fixture("errors/broken.glsl"));
    CHECK(result.diagnostics[0].line == 2);
    CHECK_THAT(result.diagnostics[0].message, ContainsSubstring("undeclaredInInclude"));
}

TEST_CASE("shaderc: a missing include is an error at the #include line", "[shaderc][compiler][errors]") {
    const CompileResult result = compileFixture("errors/missing_include.frag");

    CHECK_FALSE(result.succeeded());
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics[0].file == fixture("errors/missing_include.frag"));
    CHECK(result.diagnostics[0].line == 3);
    CHECK_THAT(result.diagnostics[0].message, ContainsSubstring("does_not_exist.glsl"));
}

TEST_CASE("shaderc: link errors name the input without a line", "[shaderc][compiler][errors]") {
    const CompileResult result = compileFixture("errors/no_main.frag");

    CHECK_FALSE(result.succeeded());
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics[0].file == fixture("errors/no_main.frag"));
    CHECK(result.diagnostics[0].line == 0);
    CHECK_THAT(result.diagnostics[0].message, ContainsSubstring("entry point"));
}

TEST_CASE("shaderc: an unreadable input is an error", "[shaderc][compiler][errors]") {
    const CompileResult result = compileGlsl(fixture("does_not_exist.frag"), ShaderStage::Fragment, {});

    CHECK_FALSE(result.succeeded());
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics[0].file == fixture("does_not_exist.frag"));
    CHECK(result.diagnostics[0].line == 0);
    CHECK(result.diagnostics[0].message == "cannot read the file");
}

TEST_CASE("shaderc: engine macros and defines", "[shaderc][compiler]") {
    // macros.vert has an #error for every macro that is not as expected.
    const CompileResult defined =
        compileFixture("macros.vert", {.defines = {{"TESERRA_TEST_FLAG", ""}, {"TESERRA_TEST_VALUE", "42"}}});
    CHECK(defined.diagnostics.empty());
    CHECK(defined.succeeded());

    const CompileResult undefined = compileFixture("macros.vert");
    CHECK_FALSE(undefined.succeeded());
    REQUIRE_FALSE(undefined.diagnostics.empty());
    CHECK(undefined.diagnostics[0].line == 10);
    CHECK_THAT(undefined.diagnostics[0].message, ContainsSubstring("TESERRA_TEST_FLAG"));
}

TEST_CASE("shaderc: diagnostics in MSVC and GCC format", "[shaderc][compiler]") {
    const fs::path file = fs::path("C:/shaders") / "sprite.frag";
    const std::string shown = fs::path(file).make_preferred().string();
    const Diagnostic error{
        .severity = Severity::Error, .file = file, .line = 7, .message = "'x' : undeclared"};
    const Diagnostic warning{.severity = Severity::Warning, .file = file, .line = 3, .message = "unused"};
    const Diagnostic noLine{
        .severity = Severity::Error, .file = file, .line = 0, .message = "no entry point"};

    CHECK(formatDiagnostic(error, DiagnosticFormat::Msvc) == shown + "(7): error: 'x' : undeclared");
    CHECK(formatDiagnostic(error, DiagnosticFormat::Gcc) == shown + ":7: error: 'x' : undeclared");
    CHECK(formatDiagnostic(warning, DiagnosticFormat::Msvc) == shown + "(3): warning: unused");
    CHECK(formatDiagnostic(noLine, DiagnosticFormat::Msvc) == shown + ": error: no entry point");
    CHECK(formatDiagnostic(noLine, DiagnosticFormat::Gcc) == shown + ": error: no entry point");
#if defined(_WIN32)
    CHECK(nativeDiagnosticFormat() == DiagnosticFormat::Msvc);
#else
    CHECK(nativeDiagnosticFormat() == DiagnosticFormat::Gcc);
#endif
}
