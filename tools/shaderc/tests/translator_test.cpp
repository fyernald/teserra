// Reflection and GLSL for OpenGL from SPIRV-Cross (ADR-0017, steps 2 and 3), on the fixtures in shaders/.

#include "translator.hpp"

#include <teserra/rhi/shader_blob.hpp>

#include "compiler.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace teserra;
using namespace teserra::shaderc;
using Catch::Matchers::ContainsSubstring;
using rhi::BindingType;
using rhi::ShaderBinding;
using rhi::ShaderScalarType;
using rhi::ShaderVertexInput;

namespace {

namespace fs = std::filesystem;

fs::path fixture(std::string_view name) {
    return (fs::path(TESERRA_SHADERC_FIXTURES) / name).lexically_normal();
}

struct Translated {
    CompileResult compiled;
    Translation translation;
};

Translated translateFixture(std::string_view name) {
    const fs::path path = fixture(name);
    const std::optional<ShaderStage> stage = stageFromPath(path);
    REQUIRE(stage.has_value());
    Translated result{.compiled = compileGlsl(path, *stage, {}), .translation = {}};
    REQUIRE(result.compiled.succeeded());
    result.translation = translateForOpenGl(result.compiled.spirv, *stage, path);
    return result;
}

// The (set, binding) decorations left in a SPIR-V module: OpDecorate <id> DescriptorSet <set>.
std::vector<std::uint32_t> descriptorSetDecorations(const std::vector<std::uint32_t>& spirv) {
    constexpr std::uint32_t kOpDecorate = 71;
    constexpr std::uint32_t kDecorationDescriptorSet = 34;
    std::vector<std::uint32_t> sets;
    for (std::size_t i = 5; i < spirv.size();) { // after the 5-word header
        const std::uint32_t words = spirv[i] >> 16U;
        if ((spirv[i] & 0xFFFFU) == kOpDecorate && words == 4 && spirv[i + 2] == kDecorationDescriptorSet) {
            sets.push_back(spirv[i + 3]);
        }
        i += words == 0 ? 1 : words;
    }
    return sets;
}

std::string translationError(std::string_view name) {
    const Translated result = translateFixture(name);
    CHECK_FALSE(result.translation.succeeded());
    REQUIRE(result.translation.diagnostics.size() == 1);
    const Diagnostic& error = result.translation.diagnostics[0];
    CHECK(error.severity == Severity::Error);
    CHECK(error.file == fixture(name));
    CHECK(error.line == 0);
    return error.message;
}

} // namespace

TEST_CASE("shaderc translation: reflection of a sprite vertex shader", "[shaderc][translator]") {
    const Translated result = translateFixture("sprite/sprite.vert");

    REQUIRE(result.translation.succeeded());
    CHECK(result.translation.diagnostics.empty());
    CHECK(result.translation.reflection.bindings ==
          std::vector<ShaderBinding>{
              {.group = 0, .binding = 0, .type = BindingType::UniformBuffer, .size = 80, .name = "Frame"},
              {.group = 3, .binding = 0, .type = BindingType::UniformBuffer, .size = 64, .name = "Draw"},
          });
    // Sorted by location, not by declaration order.
    CHECK(result.translation.reflection.vertexInputs ==
          std::vector<ShaderVertexInput>{
              {.location = 0, .scalarType = ShaderScalarType::Float, .components = 3, .name = "position"},
              {.location = 1, .scalarType = ShaderScalarType::Float, .components = 2, .name = "uv"},
              {.location = 2, .scalarType = ShaderScalarType::Float, .components = 4, .name = "color"},
              {.location = 3, .scalarType = ShaderScalarType::Uint, .components = 1, .name = "layer"},
          });
    CHECK(result.translation.reflection.pushConstantSize == 0);
}

TEST_CASE("shaderc translation: reflection of a sprite fragment shader", "[shaderc][translator]") {
    const Translated result = translateFixture("sprite/sprite.frag");

    REQUIRE(result.translation.succeeded());
    CHECK(result.translation.reflection.bindings ==
          std::vector<ShaderBinding>{
              {.group = 0, .binding = 0, .type = BindingType::UniformBuffer, .size = 80, .name = "Frame"},
              {.group = 2, .binding = 0, .type = BindingType::UniformBuffer, .size = 16, .name = "Material"},
              {.group = 2, .binding = 1, .type = BindingType::SampledTexture, .size = 0, .name = "albedo"},
          });
    CHECK(result.translation.reflection.vertexInputs.empty()); // fragment inputs are not vertex inputs
}

TEST_CASE("shaderc translation: storage buffers in a compute shader", "[shaderc][translator]") {
    const Translated result = translateFixture("fill.comp");

    REQUIRE(result.translation.succeeded());
    // The size of a block that holds only a runtime-sized array is 0.
    CHECK(result.translation.reflection.bindings ==
          std::vector<ShaderBinding>{
              {.group = 0, .binding = 0, .type = BindingType::StorageBuffer, .size = 0, .name = "Values"},
          });
}

TEST_CASE("shaderc translation: the GLSL uses flat bindings and no sets", "[shaderc][translator]") {
    const Translated vertex = translateFixture("sprite/sprite.vert");
    const Translated fragment = translateFixture("sprite/sprite.frag");
    REQUIRE(vertex.translation.succeeded());
    REQUIRE(fragment.translation.succeeded());

    const std::string& vs = vertex.translation.glsl;
    const std::string& fs = fragment.translation.glsl;
    CHECK_THAT(vs, ContainsSubstring("#version 450"));
    CHECK_THAT(vs, ContainsSubstring("layout(binding = 0, std140) uniform Frame"));
    CHECK_THAT(vs, ContainsSubstring("layout(binding = 24, std140) uniform Draw"));
    CHECK_THAT(fs, ContainsSubstring("layout(binding = 0, std140) uniform Frame"));
    CHECK_THAT(fs, ContainsSubstring("layout(binding = 16, std140) uniform Material"));
    CHECK_THAT(fs, ContainsSubstring("layout(binding = 17) uniform sampler2D albedo"));
    for (const std::string* glsl : {&vs, &fs}) {
        CHECK_FALSE(glsl->find("set =") != std::string::npos);
        CHECK_FALSE(glsl->find("GL_GOOGLE_include_directive") != std::string::npos);
    }
    // Names survive, which keeps the GLSL readable in graphics debuggers.
    CHECK_THAT(vs, ContainsSubstring("position"));
    CHECK_THAT(vs, ContainsSubstring("viewProjection"));
}

TEST_CASE("shaderc translation: the SPIR-V keeps its descriptor sets", "[shaderc][translator]") {
    const Translated result = translateFixture("sprite/sprite.frag");
    REQUIRE(result.translation.succeeded());

    // translateForOpenGl() remaps a copy; the module that goes into the blob is glslang's, sets and all.
    std::vector<std::uint32_t> sets = descriptorSetDecorations(result.compiled.spirv);
    std::ranges::sort(sets);
    CHECK(sets == std::vector<std::uint32_t>{0, 2, 2});
}

TEST_CASE("shaderc translation: resources the RHI cannot bind are errors", "[shaderc][translator][errors]") {
    CHECK(translationError("unsupported/set_out_of_range.frag") ==
          "'Extra' is at set 4, binding 0; sets are 0 to 3 and bindings 0 to 7");
    CHECK(translationError("unsupported/binding_out_of_range.frag") ==
          "'overflow' is at set 1, binding 8; sets are 0 to 3 and bindings 0 to 7");
    CHECK(translationError("unsupported/same_binding.frag") ==
          "'first' and 'second' are both at set 2, binding 0");
    CHECK(translationError("unsupported/texture_array.frag") ==
          "'layers' is an array of resources, which v1 cannot bind");
    CHECK(translationError("unsupported/push_constants.vert") ==
          "'Constants' is a push-constant block; push constants are not supported yet (#135)");
    CHECK(translationError("unsupported/matrix_input.vert") ==
          "vertex input 'instanceTransform' is not a scalar or vector of 32-bit floats or integers, which v1 "
          "vertex layouts cannot feed");

    const Translated separate = translateFixture("unsupported/separate_sampler.frag");
    CHECK_FALSE(separate.translation.succeeded());
    REQUIRE(separate.translation.diagnostics.size() == 2);
    CHECK(separate.translation.diagnostics[0].message ==
          "'image' is a separate texture; v1 binds textures combined with a sampler: use sampler2D");
    CHECK(separate.translation.diagnostics[1].message ==
          "'linearSampler' is a separate sampler; v1 binds textures combined with a sampler: use sampler2D");
}
