// OpenGL graphics pipelines against the fake driver of fake_gl.hpp: program linking and its cache, vertex
// arrays from vertex layouts, the captured state, and the state cache that binds pipelines without redundant
// GL calls. opengl_gpu_test.cpp creates and binds a pipeline on a real driver.

#include <teserra/rhi/device.hpp>
#include <teserra/rhi/shader.hpp>
#include <teserra/rhi/shader_blob.hpp>
#include <teserra/rhi_opengl/opengl_backend.hpp>
#include <teserra/test/assert.hpp>
#include <teserra/test/log_capture.hpp>

#include "fake_gl.hpp"
#include "gl_pipeline_state.hpp"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <initializer_list>
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

constexpr std::array kBuffers{VertexBufferLayout{.stride = 12},
                              VertexBufferLayout{.stride = 8, .stepRate = VertexStepRate::PerInstance}};
constexpr std::array kAttributes{
    VertexAttribute{.location = 0, .bufferIndex = 0, .format = VertexFormat::Float3},
    VertexAttribute{.location = 1, .bufferIndex = 1, .format = VertexFormat::Unorm8x4},
    VertexAttribute{.location = 2, .bufferIndex = 1, .format = VertexFormat::Uint16x2, .offset = 4},
};

std::unique_ptr<Device> requireOpenGl(test::FakeGlWindow& window, bool validation = true) {
    Result<std::unique_ptr<Device>> device = createDevice(
        {.backend = Backend::OpenGL, .enableValidation = validation, .enableBackendDebug = false}, &window,
        std::array{opengl::backendFactory()});
    if (!device) {
        FAIL(std::format("{}", device.error()));
    }
    return std::move(device).value();
}

// A vertex and a fragment shader and the GL names they got.
struct Shaders {
    explicit Shaders(Device& device, test::FakeGlState& gl) {
        vertex = *device.createShaderModule({.stage = ShaderStage::Vertex,
                                             .blob = temporaryGlslBlob(kVertexSource),
                                             .debugName = "Triangle vertex"});
        vertexName = gl.lastName;
        fragment = *device.createShaderModule({.stage = ShaderStage::Fragment,
                                               .blob = temporaryGlslBlob(kFragmentSource),
                                               .debugName = "Triangle fragment"});
        fragmentName = gl.lastName;
    }

    // A pipeline drawing into the swapchain's Rgba8Srgb image.
    [[nodiscard]] GraphicsPipelineDesc pipeline(std::string_view name) const {
        return {
            .vertexShader = vertex,
            .fragmentShader = fragment,
            .vertexBuffers = kBuffers,
            .vertexAttributes = kAttributes,
            .targets = {.colorFormats = {Format::Rgba8Srgb}},
            .debugName = name,
        };
    }

    ShaderModuleHandle vertex;
    ShaderModuleHandle fragment;
    GLuint vertexName = 0;
    GLuint fragmentName = 0;
};

// Submits one render pass into `target` that binds `pipelines` in order, and returns the GL calls of the
// submit.
Calls submitPass(Device& device, test::FakeGlState& gl, TextureHandle target, LoadOp loadOp,
                 std::initializer_list<GraphicsPipelineHandle> pipelines) {
    device.beginFrame();
    CommandList& cmd = device.acquireCommandList();
    cmd.beginRenderPass({.colorAttachments = {{.texture = target, .loadOp = loadOp}}});
    for (const GraphicsPipelineHandle pipeline : pipelines) {
        cmd.bindPipeline(pipeline);
    }
    cmd.endRenderPass();
    gl.calls.clear();
    device.submit(cmd);
    device.endFrame();
    return gl.calls;
}

} // namespace

TEST_CASE("OpenGL backend: a pipeline links a program and builds a vertex array", "[rhi_opengl][pipeline]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;
    const Shaders shaders(*device, gl);
    const GLuint program = gl.lastName + 1;
    const GLuint vertexArray = gl.lastName + 2;
    gl.calls.clear();

    const auto pipeline = device->createGraphicsPipeline(shaders.pipeline("Triangle"));

    REQUIRE(pipeline.has_value());
    const Calls expected{
        std::format("CreateProgram() = {}", program),
        std::format("AttachShader({}, {})", program, shaders.vertexName),
        std::format("AttachShader({}, {})", program, shaders.fragmentName),
        std::format("LinkProgram({})", program),
        std::format("DetachShader({}, {})", program, shaders.vertexName),
        std::format("DetachShader({}, {})", program, shaders.fragmentName),
        std::format("ObjectLabel(GL_PROGRAM, {}, Triangle vertex + Triangle fragment)", program),
        std::format("CreateVertexArrays() = {}", vertexArray),
        // A Float3 position from buffer 0.
        std::format("EnableVertexArrayAttrib({}, 0)", vertexArray),
        std::format("VertexArrayAttribFormat({}, 0, 3 GL_FLOAT, offset 0)", vertexArray),
        std::format("VertexArrayAttribBinding({}, 0, 0)", vertexArray),
        // A normalized color and integer data from buffer 1.
        std::format("EnableVertexArrayAttrib({}, 1)", vertexArray),
        std::format("VertexArrayAttribFormat({}, 1, 4 GL_UNSIGNED_BYTE normalized, offset 0)", vertexArray),
        std::format("VertexArrayAttribBinding({}, 1, 1)", vertexArray),
        std::format("EnableVertexArrayAttrib({}, 2)", vertexArray),
        std::format("VertexArrayAttribIFormat({}, 2, 2 GL_UNSIGNED_SHORT, offset 4)", vertexArray),
        std::format("VertexArrayAttribBinding({}, 2, 1)", vertexArray),
        // Buffer 1 advances per instance.
        std::format("VertexArrayBindingDivisor({}, 0, 0)", vertexArray),
        std::format("VertexArrayBindingDivisor({}, 1, 1)", vertexArray),
        std::format("ObjectLabel(GL_VERTEX_ARRAY, {}, Triangle)", vertexArray),
    };
    CHECK(gl.calls == expected);

    device->destroy(*pipeline);
    device->destroy(GraphicsPipelineHandle{}); // null handles are ignored
    CHECK(gl.livePrograms.empty());
    CHECK(gl.liveVertexArrays.empty());
}

TEST_CASE("OpenGL backend: pipelines with the same shaders share a program", "[rhi_opengl][pipeline]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;
    const Shaders shaders(*device, gl);

    const GraphicsPipelineHandle first = *device->createGraphicsPipeline(shaders.pipeline("First"));
    gl.calls.clear();
    GraphicsPipelineDesc blended = shaders.pipeline("Blended");
    blended.colorBlend[0] = {.enabled = true};
    const GraphicsPipelineHandle second = *device->createGraphicsPipeline(blended);

    CHECK(std::ranges::none_of(gl.calls,
                               [](const std::string& call) { return call.starts_with("CreateProgram"); }));
    CHECK(gl.livePrograms.size() == 1);
    CHECK(gl.liveVertexArrays.size() == 2);

    device->destroy(first);
    CHECK(gl.livePrograms.size() == 1); // still used by the second pipeline
    device->destroy(second);
    CHECK(gl.livePrograms.empty());
    CHECK(gl.liveVertexArrays.empty());
}

TEST_CASE("OpenGL backend: the device deletes the pipelines left at its end", "[rhi_opengl][pipeline]") {
    test::FakeGlWindow window;
    {
        const auto device = requireOpenGl(window);
        const Shaders shaders(*device, window.context.state);
        static_cast<void>(*device->createGraphicsPipeline(shaders.pipeline("Left over")));
        CHECK(window.context.state.livePrograms.size() == 1);
    }
    CHECK(window.context.state.livePrograms.empty());
    CHECK(window.context.state.liveVertexArrays.empty());
    CHECK(window.context.state.shaders.empty());
}

TEST_CASE("OpenGL backend: a link failure is an error with the driver's log", "[rhi_opengl][pipeline]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;
    const Shaders shaders(*device, gl);
    gl.linkLog = "error: fragment input 'vColor' has no matching vertex output\n";
    const test::LogCapture log;

    const auto pipeline = device->createGraphicsPipeline(shaders.pipeline("Broken"));

    REQUIRE_FALSE(pipeline.has_value());
    CHECK(pipeline.error().code == ErrorCode::CompilationFailed);
    const std::string message = "Graphics pipeline 'Broken' failed to link:\nerror: fragment input 'vColor' "
                                "has no matching vertex output";
    CHECK(pipeline.error().message == message);
    CHECK(gl.livePrograms.empty());
    CHECK(gl.liveVertexArrays.empty());
    REQUIRE(log.records().size() == 1);
    CHECK(log.records()[0].level == LogLevel::Error);
    CHECK(log.records()[0].message == message);
}

TEST_CASE("OpenGL backend: a depth bias clamp is ignored with a warning", "[rhi_opengl][pipeline]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    const Shaders shaders(*device, window.context.state);
    GraphicsPipelineDesc desc = shaders.pipeline("Shadow");
    desc.raster.depthBias = {.constantFactor = 2.0F, .slopeFactor = 1.5F, .clamp = 0.01F};
    const test::LogCapture log;

    CHECK(device->createGraphicsPipeline(desc).has_value());

    REQUIRE(log.records().size() == 1);
    CHECK(log.records()[0].level == LogLevel::Warn);
    CHECK(log.records()[0].message ==
          "Graphics pipeline 'Shadow': the depth bias clamp needs OpenGL 4.6 and is ignored");
}

TEST_CASE("OpenGL backend: binding pipelines issues only the state that changes", "[rhi_opengl][pipeline]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;
    const Shaders shaders(*device, gl);
    GraphicsPipelineDesc opaqueDesc = shaders.pipeline("Opaque");
    opaqueDesc.raster.cullMode = CullMode::Back;
    const GraphicsPipelineHandle opaque = *device->createGraphicsPipeline(opaqueDesc);
    const GLuint program = gl.lastName - 1;
    const GLuint opaqueArray = gl.lastName;
    GraphicsPipelineDesc blendedDesc = opaqueDesc;
    blendedDesc.debugName = "Blended";
    blendedDesc.colorBlend[0] = {.enabled = true,
                                 .srcColor = BlendFactor::SrcAlpha,
                                 .dstColor = BlendFactor::OneMinusSrcAlpha,
                                 .dstAlpha = BlendFactor::OneMinusSrcAlpha};
    const GraphicsPipelineHandle blended = *device->createGraphicsPipeline(blendedDesc);
    const GLuint blendedArray = gl.lastName;
    const TextureHandle image = device->swapchain()->acquireNextImage();

    // A pass that binds nothing: its begin calls, then "Disable(GL_SCISSOR_TEST)" at its end. The first pass
    // also creates the image's framebuffer, so the second one is the reference.
    static_cast<void>(submitPass(*device, gl, image, LoadOp::Load, {}));
    const Calls pass = submitPass(*device, gl, image, LoadOp::Load, {});
    REQUIRE_FALSE(pass.empty());
    const auto passWith = [&](const Calls& binds) {
        Calls calls(pass.begin(), pass.end() - 1);
        calls.insert(calls.end(), binds.begin(), binds.end());
        calls.push_back(pass.back());
        return calls;
    };

    // The first bind sets what differs from the GL defaults the cache started from.
    CHECK(submitPass(*device, gl, image, LoadOp::Load, {opaque}) ==
          passWith({std::format("UseProgram({})", program), std::format("BindVertexArray({})", opaqueArray),
                    "Enable(GL_CULL_FACE)"}));
    // Binding it again, twice, in a later frame: no GL call at all.
    CHECK(submitPass(*device, gl, image, LoadOp::Load, {opaque, opaque}) == pass);
    // A pipeline that differs in blending only: its vertex array and the blend calls.
    CHECK(submitPass(*device, gl, image, LoadOp::Load, {blended}) ==
          passWith(
              {std::format("BindVertexArray({})", blendedArray), "Enablei(GL_BLEND, 0)",
               "BlendFuncSeparatei(0, GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA GL_ONE GL_ONE_MINUS_SRC_ALPHA)"}));
    // And back.
    CHECK(submitPass(*device, gl, image, LoadOp::Load, {opaque}) ==
          passWith({std::format("BindVertexArray({})", opaqueArray), "Disablei(GL_BLEND, 0)",
                    "BlendFuncSeparatei(0, GL_ONE GL_ZERO GL_ONE GL_ZERO)"}));

    // Deleting the bound vertex array unbinds it first, so GL never refers to a deleted object.
    gl.calls.clear();
    device->destroy(opaque);
    CHECK(gl.calls == Calls{"BindVertexArray(0)", std::format("DeleteVertexArrays({})", opaqueArray)});
}

TEST_CASE("OpenGL backend: clears write every component whatever pipeline was bound",
          "[rhi_opengl][pipeline]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;
    const Shaders shaders(*device, gl);
    GraphicsPipelineDesc redOnly = shaders.pipeline("Red only");
    redOnly.colorBlend[0].writeMask = ColorWriteMask::Red;
    const GraphicsPipelineHandle pipeline = *device->createGraphicsPipeline(redOnly);
    const TextureHandle image = device->swapchain()->acquireNextImage();

    const Calls bound = submitPass(*device, gl, image, LoadOp::Load, {pipeline});
    CHECK(std::ranges::count(bound, std::string("ColorMaski(0, R---)")) == 1);

    const Calls cleared = submitPass(*device, gl, image, LoadOp::Clear, {});
    const auto mask = std::ranges::find(cleared, std::string("ColorMaski(0, RGBA)"));
    const auto clear = std::ranges::find_if(
        cleared, [](const std::string& call) { return call.starts_with("ClearNamedFramebufferfv"); });
    REQUIRE(mask != cleared.end());
    REQUIRE(clear != cleared.end());
    CHECK(mask < clear);
    // Once restored, a further clear needs no call.
    CHECK(std::ranges::count(submitPass(*device, gl, image, LoadOp::Clear, {}),
                             std::string("ColorMaski(0, RGBA)")) == 0);
}

TEST_CASE("OpenGL backend: pipeline state is captured as GL values", "[rhi_opengl][pipeline]") {
    using opengl::capturePipelineState;
    using opengl::GlPipelineState;

    const GlPipelineState defaults = capturePipelineState({});
    CHECK(defaults == GlPipelineState{});
    CHECK(defaults.frontFace == GL_CCW);

    const GlPipelineState raster = capturePipelineState({.raster = {.cullMode = CullMode::Front,
                                                                    .frontFace = FrontFace::Clockwise,
                                                                    .fillMode = FillMode::Line,
                                                                    .depthBias = {.constantFactor = 4.0F}}});
    CHECK(raster.cullEnabled);
    CHECK(raster.cullFace == GL_FRONT);
    CHECK(raster.frontFace == GL_CW);
    CHECK(raster.polygonMode == GL_LINE);
    CHECK(raster.polygonOffset);
    CHECK(raster.offsetUnits == 4.0F);
    CHECK(raster.offsetFactor == 0.0F);

    SECTION("depth test and write") {
        const GlPipelineState depth = capturePipelineState(
            {.depthStencil = {
                 .depthTest = true, .depthWrite = true, .depthCompare = CompareOp::LessOrEqual}});
        CHECK(depth.depthTest);
        CHECK(depth.depthWrite);
        CHECK(depth.depthFunc == GL_LEQUAL);
    }
    SECTION("depth test without write") {
        const GlPipelineState depth =
            capturePipelineState({.depthStencil = {.depthTest = true, .depthCompare = CompareOp::Greater}});
        CHECK(depth.depthTest);
        CHECK_FALSE(depth.depthWrite);
        CHECK(depth.depthFunc == GL_GREATER);
    }
    SECTION("depth write without test: GL writes depth only while testing, so the test always passes") {
        const GlPipelineState depth = capturePipelineState({.depthStencil = {.depthWrite = true}});
        CHECK(depth.depthTest);
        CHECK(depth.depthWrite);
        CHECK(depth.depthFunc == GL_ALWAYS);
    }
    SECTION("blending per target") {
        GraphicsPipelineDesc desc;
        desc.colorBlend[1] = {.enabled = true,
                              .srcColor = BlendFactor::One,
                              .dstColor = BlendFactor::One,
                              .colorOp = BlendOp::Max,
                              .writeMask = ColorWriteMask::Red | ColorWriteMask::Alpha};
        const GlPipelineState blend = capturePipelineState(desc);
        CHECK_FALSE(blend.blend[0].enabled);
        CHECK(blend.blend[1].enabled);
        CHECK(blend.blend[1].dstColor == GL_ONE);
        CHECK(blend.blend[1].colorOp == GL_MAX);
        CHECK(blend.blend[1].writeMask == std::array<GLboolean, 4>{GL_TRUE, GL_FALSE, GL_FALSE, GL_TRUE});
    }
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("OpenGL backend: pipeline misuse is caught", "[rhi_opengl][pipeline][validation]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;
    const Shaders shaders(*device, gl);
    const auto failureOf = [](auto&& call) { return test::requireAssert(call).message(); };

    GraphicsPipelineDesc swapped = shaders.pipeline("Swapped");
    std::swap(swapped.vertexShader, swapped.fragmentShader);
    CHECK(failureOf([&] { static_cast<void>(device->createGraphicsPipeline(swapped)); }) ==
          std::format("Graphics pipeline 'Swapped': vertex shader {} is not a live vertex shader",
                      shaders.fragment));

    // Through the shared validateCommands(): the pipeline's formats against the pass.
    GraphicsPipelineDesc linear = shaders.pipeline("Linear");
    linear.targets.colorFormats[0] = Format::Rgba8Unorm;
    const GraphicsPipelineHandle mismatch = *device->createGraphicsPipeline(linear);
    const TextureHandle image = device->swapchain()->acquireNextImage();
    CHECK(failureOf([&] { static_cast<void>(submitPass(*device, gl, image, LoadOp::Load, {mismatch})); }) ==
          "Pipeline 'Linear' declares Rgba8Unorm for color target 0, but the render pass has Rgba8Srgb");
    device->endFrame();

    device->destroy(mismatch);
    CHECK(failureOf([&] { device->destroy(mismatch); }) ==
          std::format("Destroying graphics pipeline {}, which is not alive", mismatch));
}

TEST_CASE("OpenGL backend: bind groups and shader reflection go through the shared validation",
          "[rhi_opengl][pipeline][validation]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    const auto failureOf = [](auto&& call) { return test::requireAssert(call).message(); };
    CHECK(device->limits().maxBindGroups == kMaxBindGroups);
    CHECK(device->limits().maxBindingsPerGroup == kMaxBindingsPerGroup);

    constexpr std::array<u32, 6> kSpirvWords{0x07230203, 0x00010500, 0x0008000B, 42, 0, 0x00020011};
    const std::vector<std::byte> blob = writeShaderBlob({
        .stage = ShaderStage::Vertex,
        .spirv = std::as_bytes(std::span(kSpirvWords)),
        .glsl = kVertexSource,
        .reflection = {.bindings = {{.type = BindingType::UniformBuffer, .size = 64, .name = "Frame"}}},
    });
    const ShaderModuleHandle vertex =
        *device->createShaderModule({.stage = ShaderStage::Vertex, .blob = blob, .debugName = "VS"});
    const auto pipelineDesc = [&](PipelineLayoutHandle layout) {
        return GraphicsPipelineDesc{.vertexShader = vertex,
                                    .layout = layout,
                                    .targets = {.colorFormats = {Format::Rgba8Srgb}},
                                    .debugName = "Sprites"};
    };
    CHECK(failureOf([&] { static_cast<void>(device->createGraphicsPipeline(pipelineDesc({}))); }) ==
          "Graphics pipeline 'Sprites': vertex shader 'VS' uses uniform buffer 'Frame' at (group 0, binding "
          "0), "
          "but the pipeline has no layout");

    constexpr std::array kEntries{BindGroupLayoutEntry{.type = BindingType::UniformBuffer}};
    const BindGroupLayoutHandle frame = *device->createBindGroupLayout({.entries = kEntries});
    const PipelineLayoutHandle layout = *device->createPipelineLayout({.groups = std::array{frame}});
    const GraphicsPipelineHandle pipeline = *device->createGraphicsPipeline(pipelineDesc(layout));
    const BufferHandle uniforms = *device->createBuffer({.sizeInBytes = 256, .usage = BufferUsage::Uniform});
    const std::array resources{BindGroupEntry{.buffer = {.buffer = uniforms}}};
    const BindGroupHandle group = *device->createBindGroup({.layout = frame, .entries = resources});
    CHECK(failureOf([&] {
              static_cast<void>(device->createBindGroup({.layout = frame, .debugName = "Empty"}));
          }) == "Bind group 'Empty' has 0 entries for the 1 binding(s) of layout ''");

    // The pipeline keeps its layout's groups, so draws still need set 0 once the layouts are gone.
    device->destroy(layout);
    device->destroy(frame);
    const TextureHandle image = device->swapchain()->acquireNextImage();
    device->beginFrame();
    CommandList& cmd = device->acquireCommandList();
    cmd.beginRenderPass({.colorAttachments = {{.texture = image}}});
    cmd.bindPipeline(pipeline);
    cmd.draw({.vertexCount = 3});
    cmd.endRenderPass();
    CHECK(failureOf([&] { device->submit(cmd); }) ==
          "draw needs a bind group at set 0 for pipeline 'Sprites', and none is bound");
    device->endFrame();

    device->destroy(group);
    CHECK(failureOf([&] { device->destroy(group); }) ==
          std::format("Destroying bind group {}, which is not alive", group));
}

TEST_CASE("OpenGL backend: without validation, replay still catches stale pipelines",
          "[rhi_opengl][pipeline]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window, /*validation=*/false);
    test::FakeGlState& gl = window.context.state;
    const Shaders shaders(*device, gl);
    const GraphicsPipelineHandle stale = *device->createGraphicsPipeline(shaders.pipeline("Stale"));
    device->destroy(stale);
    const TextureHandle image = device->swapchain()->acquireNextImage();

    CHECK(test::requireAssert([&] {
              static_cast<void>(submitPass(*device, gl, image, LoadOp::Load, {stale}));
          }).message() == std::format("bindPipeline uses pipeline {}, which is not alive", stale));
    device->endFrame();
}
#endif
