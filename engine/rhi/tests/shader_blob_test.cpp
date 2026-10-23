// .tshader blobs: the flat binding function, the round trip and the reader's checks.

#include <teserra/rhi/shader_blob.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace teserra;
using namespace teserra::rhi;

namespace {

constexpr std::array<u32, 6> kSpirvWords{0x07230203, 0x00010500, 0x0008000B, 42, 0, 0x00020011};
constexpr std::string_view kGlsl =
    "#version 450\nlayout(binding = 16, std140) uniform Material { vec4 tint; };\n";

ShaderReflection sampleReflection() {
    return {
        .bindings =
            {{.group = 0, .binding = 0, .type = BindingType::UniformBuffer, .size = 80, .name = "Frame"},
             {.group = 2, .binding = 1, .type = BindingType::SampledTexture, .size = 0, .name = "albedo"}},
        .vertexInputs =
            {{.location = 0, .scalarType = ShaderScalarType::Float, .components = 3, .name = "position"},
             {.location = 1, .scalarType = ShaderScalarType::Uint, .components = 1, .name = "id"}},
    };
}

std::vector<std::byte> sampleBlob() {
    return writeShaderBlob({.stage = ShaderStage::Fragment,
                            .spirv = std::as_bytes(std::span(kSpirvWords)),
                            .glsl = kGlsl,
                            .reflection = sampleReflection()});
}

u32 wordAt(const std::vector<std::byte>& blob, std::size_t offset) {
    u32 value = 0;
    std::memcpy(&value, blob.data() + offset, sizeof(value));
    return value;
}

void setWord(std::vector<std::byte>& blob, std::size_t offset, u32 value) {
    std::memcpy(blob.data() + offset, &value, sizeof(value));
}

// Offsets in a version 1 blob: the section table starts at 16, entries are {type, offset, size}.
constexpr std::size_t sectionEntry(std::size_t index) {
    return 16 + index * 12;
}

std::size_t reflectionOffset(const std::vector<std::byte>& blob) {
    return wordAt(blob, sectionEntry(2) + 4);
}

// The error message of reading `blob`, which must fail with `code`.
std::string failure(const std::vector<std::byte>& blob, ErrorCode code = ErrorCode::InvalidData) {
    const Result<ShaderBlobContents> result = readShaderBlob(blob);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == code);
    CHECK(result.error().module == Module::Rhi);
    return result.error().message;
}

} // namespace

TEST_CASE("flatBinding: group * 8 + binding, as the OpenGL backend and teserra_shaderc use it",
          "[rhi][shader_blob]") {
    static_assert(flatBinding(0, 0) == 0);
    static_assert(flatBinding(kMaxBindGroups - 1, kMaxBindingsPerGroup - 1) == 31);
    CHECK(flatBinding(0, 3) == 3);
    CHECK(flatBinding(2, 0) == 16);
    CHECK(flatBinding(2, 1) == 17);
    CHECK(flatBinding(3, 0) == 24);
}

TEST_CASE("Shader blobs: what is written is read back, in place", "[rhi][shader_blob]") {
    const std::vector<std::byte> blob = sampleBlob();

    CHECK(blob.size() % 4 == 0);
    CHECK(wordAt(blob, 4) == kShaderBlobVersion); // the version field
    const Result<ShaderBlobContents> contents = readShaderBlob(blob);
    REQUIRE(contents.has_value());
    CHECK(contents->stage == ShaderStage::Fragment);
    REQUIRE(contents->spirv.size() == sizeof(kSpirvWords));
    CHECK(std::memcmp(contents->spirv.data(), kSpirvWords.data(), sizeof(kSpirvWords)) == 0);
    CHECK(contents->glsl == kGlsl);
    CHECK(contents->reflection == sampleReflection());
    // The views point into the blob.
    CHECK(contents->spirv.data() >= blob.data());
    CHECK(contents->spirv.data() + contents->spirv.size() <= blob.data() + blob.size());
    CHECK(reinterpret_cast<const std::byte*>(contents->glsl.data()) > blob.data());
}

TEST_CASE("Shader blobs: no GLSL, no reflection entries, compute stage", "[rhi][shader_blob]") {
    const std::vector<std::byte> blob = writeShaderBlob(
        {.stage = ShaderStage::Compute, .spirv = std::as_bytes(std::span(kSpirvWords)), .reflection = {}});

    const Result<ShaderBlobContents> contents = readShaderBlob(blob);
    REQUIRE(contents.has_value());
    CHECK(contents->stage == ShaderStage::Compute);
    CHECK(contents->glsl.empty());
    CHECK(contents->reflection == ShaderReflection{});
}

TEST_CASE("Shader blobs: sections of unknown types are skipped", "[rhi][shader_blob]") {
    std::vector<std::byte> blob = sampleBlob();
    setWord(blob, sectionEntry(1), 99); // the GLSL section becomes a section of a later format revision

    const Result<ShaderBlobContents> contents = readShaderBlob(blob);
    REQUIRE(contents.has_value());
    CHECK(contents->glsl.empty());
    CHECK(contents->reflection == sampleReflection());

    // 257 is unknown too, not SPIR-V (1) seen through a narrowing conversion.
    setWord(blob, sectionEntry(1), 257);
    CHECK(readShaderBlob(blob).has_value());
}

TEST_CASE("Shader blobs: every truncation is rejected", "[rhi][shader_blob]") {
    const std::vector<std::byte> blob = sampleBlob();
    for (std::size_t size = 0; size < blob.size(); ++size) {
        INFO("first " << size << " bytes");
        CHECK_FALSE(readShaderBlob(std::span(blob).first(size)).has_value());
    }
}

TEST_CASE("Shader blobs: header and section table checks", "[rhi][shader_blob]") {
    CHECK(failure(std::vector<std::byte>(8)) == "The shader blob is 8 bytes, too short for a .tshader blob");

    std::vector<std::byte> blob = sampleBlob();
    blob[0] = std::byte{'X'};
    CHECK(failure(blob) == "The shader blob is not a .tshader blob");

    blob = sampleBlob();
    setWord(blob, 4, 2);
    CHECK(failure(blob, ErrorCode::Unsupported) ==
          "The shader blob has format version 2; this build reads version 1");

    blob = sampleBlob();
    setWord(blob, 8, static_cast<u32>(ShaderStage::Vertex | ShaderStage::Fragment));
    CHECK(failure(blob) == "The shader blob has an invalid stage 3");

    blob = sampleBlob();
    setWord(blob, 12, 1000);
    CHECK(failure(blob) == "The section table of the shader blob (1000 sections) is truncated");

    blob = sampleBlob();
    setWord(blob, sectionEntry(1) + 4, static_cast<u32>(blob.size()) - 4); // GLSL runs past the end
    CHECK(failure(blob) == "Section 1 of the shader blob lies outside the blob");

    blob = sampleBlob();
    setWord(blob, sectionEntry(1), 1); // a second SPIR-V section
    CHECK(failure(blob) == "The shader blob has two sections of type 1");

    blob = sampleBlob();
    setWord(blob, sectionEntry(0) + 8, 6); // not a whole number of words
    CHECK(failure(blob) == "The shader blob has no SPIR-V of whole words");

    blob = sampleBlob();
    setWord(blob, sectionEntry(2), 99);
    CHECK(failure(blob) == "The shader blob has no reflection section");
}

TEST_CASE("Shader blobs: reflection checks", "[rhi][shader_blob]") {
    // Reflection layout: 3 counts, then 24-byte binding records {group, binding, type, size, name offset,
    // name length}, then 20-byte input records {location, scalar type, components, name offset, length}.
    const std::size_t reflection = reflectionOffset(sampleBlob());
    const std::size_t binding0 = reflection + 12;
    const std::size_t input0 = binding0 + std::size_t{2} * 24;

    std::vector<std::byte> blob = sampleBlob();
    setWord(blob, reflection, 1'000'000);
    CHECK(failure(blob) ==
          "The reflection section of the shader blob is too short for 1000000 bindings and 2 vertex inputs");

    blob = sampleBlob();
    setWord(blob, binding0, kMaxBindGroups);
    CHECK(failure(blob) ==
          "Binding 0 of the shader blob is at group 4, binding 0; the limits are 4 groups of 8");

    blob = sampleBlob();
    setWord(blob, binding0 + 24 + 4, kMaxBindingsPerGroup);
    CHECK(failure(blob) ==
          "Binding 1 of the shader blob is at group 2, binding 8; the limits are 4 groups of 8");

    blob = sampleBlob();
    setWord(blob, binding0 + 8, 7);
    CHECK(failure(blob) == "Binding 0 of the shader blob has an invalid type 7");

    blob = sampleBlob();
    setWord(blob, binding0 + 20, 10'000);
    CHECK(failure(blob) == "The name of binding 0 lies outside the shader blob's reflection section");

    blob = sampleBlob();
    setWord(blob, input0 + 8, 5);
    CHECK(failure(blob) == "Vertex input 0 of the shader blob has an invalid type (0, 5 components)");

    blob = sampleBlob();
    setWord(blob, input0 + 20 + 4, 3);
    CHECK(failure(blob) == "Vertex input 1 of the shader blob has an invalid type (3, 1 components)");

    blob = sampleBlob();
    setWord(blob, input0 + 12, 10'000);
    CHECK(failure(blob) == "The name of vertex input 0 lies outside the shader blob's reflection section");
}
