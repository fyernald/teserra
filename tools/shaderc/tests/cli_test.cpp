// The command line of teserra_shaderc: arguments, output file and exit codes.

#include "cli.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace teserra::shaderc;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;

namespace {

namespace fs = std::filesystem;

struct Run {
    int exitCode = 0;
    std::string out;
    std::string err;
};

Run runShaderc(std::initializer_list<std::string> args) {
    const std::vector<std::string_view> views(args.begin(), args.end());
    std::ostringstream out;
    std::ostringstream err;
    const int exitCode = run(views, out, err);
    return {exitCode, out.str(), err.str()};
}

std::string fixture(std::string_view name) {
    return (fs::path(TESERRA_SHADERC_FIXTURES) / name).lexically_normal().string();
}

// A fresh path for one test's output; test cases run in separate processes, maybe in parallel.
fs::path outputPath(std::string_view name) {
    const fs::path path = fs::path(TESERRA_SHADERC_TEST_OUTPUT) / name;
    fs::remove_all(path.parent_path());
    return path;
}

std::vector<std::uint32_t> readWords(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::vector<std::uint32_t> words(static_cast<std::size_t>(fs::file_size(path)) / sizeof(std::uint32_t));
    file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(words.size() * 4));
    return words;
}

} // namespace

TEST_CASE("shaderc CLI: compiles a shader into the -o file", "[shaderc][cli]") {
    const fs::path output = outputPath("compiles/nested/dir/triangle.vert.spv");

    const Run result = runShaderc({fixture("triangle.vert"), "-o", output.string()});

    CHECK(result.exitCode == kExitSuccess);
    CHECK(result.err.empty());
    REQUIRE(fs::is_regular_file(output)); // the directories are created
    CHECK(fs::file_size(output) % 4 == 0);
    const std::vector<std::uint32_t> words = readWords(output);
    REQUIRE_FALSE(words.empty());
    CHECK(words[0] == 0x07230203);
}

TEST_CASE("shaderc CLI: -I and -D, attached or separate", "[shaderc][cli]") {
    const fs::path output = outputPath("options/out.spv");
    const std::string includeDir = fixture("include_dir");

    CHECK(runShaderc({fixture("triangle.frag"), "-o", output.string(), "-I", includeDir}).exitCode ==
          kExitSuccess);
    CHECK(runShaderc({fixture("triangle.frag"), "-o" + output.string(), "-I" + includeDir}).exitCode ==
          kExitSuccess);
    CHECK(runShaderc({fixture("macros.vert"), "-o", output.string(), "-DTESERRA_TEST_FLAG", "-D",
                      "TESERRA_TEST_VALUE=42"})
              .exitCode == kExitSuccess);
    CHECK(fs::is_regular_file(output));
}

TEST_CASE("shaderc CLI: shader errors exit with 1 and remove the stale output", "[shaderc][cli]") {
    const fs::path output = outputPath("errors/syntax_error.frag.spv");
    fs::create_directories(output.parent_path());
    std::ofstream(output) << "stale";

    const Run msvc =
        runShaderc({fixture("errors/syntax_error.frag"), "-o", output.string(), "--diagnostics", "msvc"});

    CHECK(msvc.exitCode == kExitShaderErrors);
    CHECK_FALSE(fs::exists(output));
    const std::string file = fs::path(fixture("errors/syntax_error.frag")).make_preferred().string();
    CHECK(msvc.err == file + "(7): error: 'undeclaredColor' : undeclared identifier\n");

    const Run gcc =
        runShaderc({fixture("errors/syntax_error.frag"), "-o", output.string(), "--diagnostics=gcc"});
    CHECK(gcc.exitCode == kExitShaderErrors);
    CHECK(gcc.err == file + ":7: error: 'undeclaredColor' : undeclared identifier\n");
}

TEST_CASE("shaderc CLI: usage errors exit with 2", "[shaderc][cli]") {
    const std::string output = outputPath("usage/out.spv").string();
    const std::string input = fixture("triangle.vert");

    const auto expectUsageError = [](const Run& result, std::string_view message) {
        INFO(result.err);
        CHECK(result.exitCode == kExitUsageError);
        CHECK_THAT(result.err, StartsWith("teserra_shaderc: error: " + std::string(message)));
        CHECK_THAT(result.err, ContainsSubstring("usage: teserra_shaderc"));
    };
    expectUsageError(runShaderc({}), "no input file");
    expectUsageError(runShaderc({input}), "no output file (-o)");
    expectUsageError(runShaderc({input, "-o"}), "-o needs an output file");
    expectUsageError(runShaderc({input, "-o", output, "-I"}), "-I needs a directory");
    expectUsageError(runShaderc({input, "-o", output, "-D=1"}), "-D needs a macro name");
    expectUsageError(runShaderc({input, "-o", output, "--diagnostics", "vs"}),
                     "--diagnostics is msvc or gcc");
    expectUsageError(runShaderc({input, "-o", output, "--optimize"}), "unknown option '--optimize'");
    expectUsageError(runShaderc({input, input, "-o", output}), "more than one input file");
    expectUsageError(runShaderc({fixture("errors/unknown_stage.glsl"), "-o", output}),
                     "cannot tell the shader stage of");
    CHECK_FALSE(fs::exists(output));
}

TEST_CASE("shaderc CLI: --help prints the usage", "[shaderc][cli]") {
    const Run result = runShaderc({"--help"});

    CHECK(result.exitCode == kExitSuccess);
    CHECK_THAT(result.out, StartsWith("usage: teserra_shaderc"));
    CHECK(result.err.empty());
}
