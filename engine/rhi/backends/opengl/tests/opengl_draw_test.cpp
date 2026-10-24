// OpenGL draws against the fake driver of fake_gl.hpp: vertex and index buffer bindings applied to the bound
// pipeline's vertex array at the next draw, the draw calls, and passes starting with nothing bound.
// opengl_gpu_test.cpp draws on a real driver and checks the winding and orientation.

#include <teserra/rhi/device.hpp>
#include <teserra/rhi/shader.hpp>
#include <teserra/rhi_opengl/opengl_backend.hpp>
#include <teserra/test/assert.hpp>

#include "fake_gl.hpp"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace teserra;
using namespace teserra::rhi;

namespace {

using Calls = std::vector<std::string>;

constexpr std::string_view kVertexSource = R"(#version 450 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 vColor;
void main() { vColor = color; gl_Position = vec4(position, 1.0); }
)";

constexpr std::string_view kFragmentSource = R"(#version 450 core
layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 fragColor;
void main() { fragColor = vColor; }
)";

constexpr std::array kBuffers{VertexBufferLayout{.stride = 28}};
constexpr std::array kAttributes{
    VertexAttribute{.location = 0, .format = VertexFormat::Float3},
    VertexAttribute{.location = 1, .format = VertexFormat::Float4, .offset = 12},
};

// A device with a triangle pipeline, buffers, and the GL names they got.
struct Scene {
    explicit Scene(bool validation = true) {
        Result<std::unique_ptr<Device>> created = createDevice(
            {.backend = Backend::OpenGL, .enableValidation = validation, .enableBackendDebug = false},
            &window, std::array{opengl::backendFactory()});
        if (!created) {
            FAIL(std::format("{}", created.error()));
        }
        device = std::move(created).value();
        const ShaderModuleHandle vertex = *device->createShaderModule(
            {.stage = ShaderStage::Vertex, .blob = test::glslBlob(ShaderStage::Vertex, kVertexSource)});
        const ShaderModuleHandle fragment = *device->createShaderModule(
            {.stage = ShaderStage::Fragment, .blob = test::glslBlob(ShaderStage::Fragment, kFragmentSource)});
        pipeline = createPipeline(vertex, fragment, "Triangle");
        pipelineArray = gl().lastName;
        otherPipeline = createPipeline(vertex, fragment, "Other");
        otherArray = gl().lastName;
        vertices =
            *device->createBuffer({.sizeInBytes = 84, .usage = BufferUsage::Vertex, .debugName = "Vertices"});
        verticesName = gl().lastName;
        indices =
            *device->createBuffer({.sizeInBytes = 32, .usage = BufferUsage::Index, .debugName = "Indices"});
        indicesName = gl().lastName;
    }

    test::FakeGlState& gl() { return window.context.state; }

    [[nodiscard]] GraphicsPipelineHandle
    createPipeline(ShaderModuleHandle vertex, ShaderModuleHandle fragment, std::string_view name) const {
        return *device->createGraphicsPipeline({
            .vertexShader = vertex,
            .fragmentShader = fragment,
            .vertexBuffers = kBuffers,
            .vertexAttributes = kAttributes,
            .targets = {.colorFormats = {Format::Rgba8Srgb}},
            .debugName = name,
        });
    }

    // Submits one render pass into the swapchain image with the commands of `record`, and returns the GL
    // calls between the pass's setup and its end.
    Calls submitPass(const std::function<void(CommandList&)>& record) {
        device->beginFrame();
        CommandList& cmd = device->acquireCommandList();
        cmd.beginRenderPass({.colorAttachments = {{.texture = device->swapchain()->acquireNextImage()}}});
        record(cmd);
        cmd.endRenderPass();
        gl().calls.clear();
        device->submit(cmd);
        device->endFrame();
        const Calls& calls = gl().calls;
        const auto setup = std::ranges::find(calls, std::string("DepthRange(0 1)"));
        REQUIRE(setup != calls.end());
        return {setup + 1, calls.end() - 1}; // without "Disable(GL_SCISSOR_TEST)" at the end
    }

    test::FakeGlWindow window;
    std::unique_ptr<Device> device;
    GraphicsPipelineHandle pipeline;
    GraphicsPipelineHandle otherPipeline;
    GLuint pipelineArray = 0;
    GLuint otherArray = 0;
    BufferHandle vertices;
    BufferHandle indices;
    GLuint verticesName = 0;
    GLuint indicesName = 0;
};

} // namespace

TEST_CASE("OpenGL backend: draws apply the pass's buffers to the pipeline's vertex array once",
          "[rhi_opengl][draw]") {
    Scene scene;

    const Calls calls = scene.submitPass([&](CommandList& cmd) {
        cmd.bindPipeline(scene.pipeline);
        cmd.bindVertexBuffer(0, scene.vertices);
        cmd.bindIndexBuffer(scene.indices, IndexType::Uint16, 2);
        cmd.drawIndexed({.indexCount = 3, .firstIndex = 1});
        cmd.drawIndexed({.indexCount = 3});
        cmd.draw({.vertexCount = 3, .instanceCount = 2, .firstInstance = 1});
    });

    const GLuint program = scene.pipelineArray - 1;
    const auto drawElements = [](u32 offset) {
        return std::format(
            "DrawElements(GL_TRIANGLES, count 3, GL_UNSIGNED_SHORT, offset {}, instances 1 from 0, "
            "base vertex 0)",
            offset);
    };
    CHECK(calls ==
          Calls{
              std::format("UseProgram({})", program),
              std::format("BindVertexArray({})", scene.pipelineArray),
              std::format("VertexArrayVertexBuffer({}, 0, {}, offset 0, stride 28)", scene.pipelineArray,
                          scene.verticesName),
              std::format("VertexArrayElementBuffer({}, {})", scene.pipelineArray, scene.indicesName),
              // Byte offsets: the binding's 2 plus firstIndex 16-bit indices.
              drawElements(4),
              drawElements(2),
              "DrawArrays(GL_TRIANGLES, first 0, count 3, instances 2 from 1)",
          });
}

TEST_CASE("OpenGL backend: 32-bit indices and vertex offsets", "[rhi_opengl][draw]") {
    Scene scene;

    const Calls calls = scene.submitPass([&](CommandList& cmd) {
        cmd.bindPipeline(scene.pipeline);
        cmd.bindVertexBuffer(0, scene.vertices, 28);
        cmd.bindIndexBuffer(scene.indices, IndexType::Uint32, 8);
        cmd.drawIndexed(
            {.indexCount = 6, .instanceCount = 3, .firstIndex = 2, .vertexOffset = -1, .firstInstance = 4});
    });

    CHECK(std::ranges::count(calls, std::format("VertexArrayVertexBuffer({}, 0, {}, offset 28, stride 28)",
                                                scene.pipelineArray, scene.verticesName)) == 1);
    CHECK(calls.back() == "DrawElements(GL_TRIANGLES, count 6, GL_UNSIGNED_INT, offset 16, instances 3 from "
                          "4, base vertex -1)");
}

TEST_CASE("OpenGL backend: bindings follow the pipeline to its vertex array", "[rhi_opengl][draw]") {
    Scene scene;

    // Buffers may be bound before the pipeline, and stay bound when the pipeline changes.
    const Calls calls = scene.submitPass([&](CommandList& cmd) {
        cmd.bindVertexBuffer(0, scene.vertices);
        cmd.bindIndexBuffer(scene.indices, IndexType::Uint16);
        cmd.bindPipeline(scene.pipeline);
        cmd.drawIndexed({.indexCount = 3});
        cmd.bindPipeline(scene.otherPipeline);
        cmd.drawIndexed({.indexCount = 3});
    });

    for (const GLuint vertexArray : {scene.pipelineArray, scene.otherArray}) {
        CHECK(std::ranges::count(calls, std::format("VertexArrayVertexBuffer({}, 0, {}, offset 0, stride 28)",
                                                    vertexArray, scene.verticesName)) == 1);
        CHECK(std::ranges::count(calls, std::format("VertexArrayElementBuffer({}, {})", vertexArray,
                                                    scene.indicesName)) == 1);
    }
    CHECK(std::ranges::count_if(
              calls, [](const std::string& call) { return call.starts_with("DrawElements"); }) == 2);
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("OpenGL backend: a pass starts with no buffers bound", "[rhi_opengl][draw][validation]") {
    for (const bool validation : {true, false}) {
        INFO("validation " << validation);
        Scene scene(validation);
        static_cast<void>(scene.submitPass([&](CommandList& cmd) {
            cmd.bindPipeline(scene.pipeline);
            cmd.bindVertexBuffer(0, scene.vertices);
            cmd.draw({.vertexCount = 3});
        }));

        // The shared validation and, without it, the backend's replay say the same.
        const auto failure = test::requireAssert([&] {
            static_cast<void>(scene.submitPass([&](CommandList& cmd) {
                cmd.bindPipeline(scene.pipeline);
                cmd.draw({.vertexCount = 3});
            }));
        });
        CHECK(failure.message() == "draw needs vertex buffer 0 of pipeline 'Triangle', which is not bound");
        scene.device->endFrame();
    }
}

TEST_CASE("OpenGL backend: without validation, replay still catches stale buffers", "[rhi_opengl][draw]") {
    Scene scene(/*validation=*/false);
    const BufferHandle stale =
        *scene.device->createBuffer({.sizeInBytes = 84, .usage = BufferUsage::Vertex | BufferUsage::Index});
    scene.device->destroy(stale);

    CHECK(test::requireAssert([&] {
              static_cast<void>(scene.submitPass([&](CommandList& cmd) { cmd.bindVertexBuffer(0, stale); }));
          }).message() == std::format("bindVertexBuffers uses buffer {}, which is not alive", stale));
    scene.device->endFrame();
    CHECK(test::requireAssert([&] {
              static_cast<void>(
                  scene.submitPass([&](CommandList& cmd) { cmd.bindIndexBuffer(stale, IndexType::Uint16); }));
          }).message() == std::format("bindIndexBuffer uses buffer {}, which is not alive", stale));
    scene.device->endFrame();
}
#endif
