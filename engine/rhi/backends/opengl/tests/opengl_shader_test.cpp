// OpenGL shader modules against the fake driver of fake_gl.hpp: the GL calls of a compile, the GLSL of
// .tshader blobs, failures as errors, and blobs the backend cannot read. opengl_gpu_test.cpp compiles real
// shaders.

#include <teserra/rhi/device.hpp>
#include <teserra/rhi/shader.hpp>
#include <teserra/rhi/shader_blob.hpp>
#include <teserra/rhi_opengl/opengl_backend.hpp>
#include <teserra/test/assert.hpp>
#include <teserra/test/log_capture.hpp>

#include "fake_gl.hpp"
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace teserra;
using namespace teserra::rhi;

namespace {

constexpr std::string_view kVertexSource = R"(#version 450 core
layout(location = 0) in vec3 position;
void main() { gl_Position = vec4(position, 1.0); }
)";

constexpr std::string_view kFragmentSource = R"(#version 450 core
layout(location = 0) out vec4 color;
void main() { color = vec4(1.0); }
)";

// The backend only reads the GLSL of a .tshader blob; the SPIR-V just has to be whole words.
constexpr std::array<u32, 2> kSpirvWords{0x07230203, 0x00010500};

std::unique_ptr<Device> requireOpenGl(test::FakeGlWindow& window) {
    Result<std::unique_ptr<Device>> device =
        createDevice({.backend = Backend::OpenGL, .enableValidation = true, .enableBackendDebug = false},
                     &window, std::array{opengl::backendFactory()});
    if (!device) {
        FAIL(std::format("{}", device.error()));
    }
    return std::move(device).value();
}

} // namespace

TEST_CASE("OpenGL backend: shader modules compile the GLSL of each stage", "[rhi_opengl][shader]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;
    const GLuint first = gl.lastName + 1; // GL names of the two shaders: first, first + 1
    gl.calls.clear();

    const std::vector<std::byte> vertexBlob = test::glslBlob(ShaderStage::Vertex, kVertexSource);
    const std::vector<std::byte> fragmentBlob = test::glslBlob(ShaderStage::Fragment, kFragmentSource);
    const auto vertex = device->createShaderModule(
        {.stage = ShaderStage::Vertex, .blob = vertexBlob, .debugName = "Triangle vertex"});
    const auto fragment = device->createShaderModule({.stage = ShaderStage::Fragment, .blob = fragmentBlob});
    REQUIRE(vertex.has_value());
    REQUIRE(fragment.has_value());

    const std::vector<std::string> expected{
        std::format("CreateShader(GL_VERTEX_SHADER) = {}", first),
        std::format("ShaderSource({}, {} bytes)", first, kVertexSource.size()),
        std::format("CompileShader({})", first),
        std::format("ObjectLabel(GL_SHADER, {}, Triangle vertex)", first),
        std::format("CreateShader(GL_FRAGMENT_SHADER) = {}", first + 1),
        std::format("ShaderSource({}, {} bytes)", first + 1, kFragmentSource.size()),
        std::format("CompileShader({})", first + 1),
    };
    CHECK(gl.calls == expected);
    // The source reaches the driver unchanged, so its line numbers are the author's.
    CHECK(gl.shaders.at(first) == kVertexSource);
    CHECK(gl.shaders.at(first + 1) == kFragmentSource);

    device->destroy(*vertex);
    device->destroy(ShaderModuleHandle{}); // null handles are ignored
    CHECK(gl.calls.back() == std::format("DeleteShader({})", first));
    CHECK(gl.shaders.size() == 1);
}

TEST_CASE("OpenGL backend: the device deletes the shaders left at its end", "[rhi_opengl][shader]") {
    test::FakeGlWindow window;
    {
        const auto device = requireOpenGl(window);
        static_cast<void>(
            *device->createShaderModule({.blob = test::glslBlob(ShaderStage::Vertex, kVertexSource)}));
        CHECK(window.context.state.shaders.size() == 1);
    }
    CHECK(window.context.state.shaders.empty());
}

TEST_CASE("OpenGL backend: a compile failure is an error with the driver's log", "[rhi_opengl][shader]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;
    gl.compileLog = "0(3) : error C0000: syntax error, unexpected '}'\n";
    const test::LogCapture log;

    const auto shader =
        device->createShaderModule({.stage = ShaderStage::Fragment,
                                    .blob = test::glslBlob(ShaderStage::Fragment, kFragmentSource),
                                    .debugName = "Broken"});

    REQUIRE_FALSE(shader.has_value());
    CHECK(shader.error().code == ErrorCode::CompilationFailed);
    CHECK(shader.error().module == Module::Rhi);
    const std::string message = "Shader module 'Broken' (fragment) failed to compile:\n0(3) : error C0000: "
                                "syntax error, unexpected '}'";
    CHECK(shader.error().message == message);
    CHECK(gl.shaders.empty()); // the shader object is deleted again

    REQUIRE(log.records().size() == 1);
    CHECK(log.records()[0].level == LogLevel::Error);
    CHECK(log.records()[0].category == LogCategory::Rhi);
    CHECK(log.records()[0].message == message);
}

TEST_CASE("OpenGL backend: shader modules compile the GLSL of a .tshader blob", "[rhi_opengl][shader]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;
    const GLuint name = gl.lastName + 1;
    gl.calls.clear();

    const std::vector<std::byte> blob = writeShaderBlob({.stage = ShaderStage::Fragment,
                                                         .spirv = std::as_bytes(std::span(kSpirvWords)),
                                                         .glsl = kFragmentSource});
    const auto shader = device->createShaderModule({.stage = ShaderStage::Fragment, .blob = blob});

    REQUIRE(shader.has_value());
    CHECK(gl.calls ==
          std::vector<std::string>{std::format("CreateShader(GL_FRAGMENT_SHADER) = {}", name),
                                   std::format("ShaderSource({}, {} bytes)", name, kFragmentSource.size()),
                                   std::format("CompileShader({})", name)});
    CHECK(gl.shaders.at(name) == kFragmentSource);
}

TEST_CASE("OpenGL backend: blobs and entry points GLSL cannot take are errors", "[rhi_opengl][shader]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;
    gl.calls.clear();

    constexpr std::array<std::byte, 4> kOpaque{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    const auto opaque = device->createShaderModule({.blob = kOpaque, .debugName = "Opaque"});
    REQUIRE_FALSE(opaque.has_value());
    CHECK(opaque.error().code == ErrorCode::InvalidData);
    CHECK(opaque.error().message ==
          "Shader module 'Opaque': The shader blob is 4 bytes, too short for a .tshader blob");

    const std::vector<std::byte> fragmentBlob =
        writeShaderBlob({.stage = ShaderStage::Fragment,
                         .spirv = std::as_bytes(std::span(kSpirvWords)),
                         .glsl = kFragmentSource});
    const auto mismatch = device->createShaderModule(
        {.stage = ShaderStage::Vertex, .blob = fragmentBlob, .debugName = "Swapped"});
    REQUIRE_FALSE(mismatch.has_value());
    CHECK(mismatch.error().code == ErrorCode::InvalidArgument);
    CHECK(mismatch.error().message ==
          "Shader module 'Swapped' is a vertex shader, but its blob holds a fragment shader");

    const std::vector<std::byte> spirvOnly =
        writeShaderBlob({.stage = ShaderStage::Vertex, .spirv = std::as_bytes(std::span(kSpirvWords))});
    const auto noGlsl = device->createShaderModule({.blob = spirvOnly, .debugName = "Vulkan only"});
    REQUIRE_FALSE(noGlsl.has_value());
    CHECK(noGlsl.error().code == ErrorCode::Unsupported);
    CHECK(noGlsl.error().message == "Shader module 'Vulkan only': the blob has no GLSL for OpenGL");

    const auto named = device->createShaderModule({.blob = test::glslBlob(ShaderStage::Vertex, kVertexSource),
                                                   .entryPoint = "vertexMain",
                                                   .debugName = "Named"});
    REQUIRE_FALSE(named.has_value());
    CHECK(named.error().code == ErrorCode::Unsupported);
    CHECK(named.error().message ==
          "Shader module 'Named': GLSL has no entry point 'vertexMain'; it must be 'main'");

    CHECK(gl.calls.empty());
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("OpenGL backend: destroying a stale shader module asserts", "[rhi_opengl][shader]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    const ShaderModuleHandle shader =
        *device->createShaderModule({.blob = test::glslBlob(ShaderStage::Vertex, kVertexSource)});
    device->destroy(shader);

    CHECK(test::requireAssert([&] { device->destroy(shader); }).message() ==
          std::format("Destroying shader module {}, which is not alive", shader));
}
#endif
