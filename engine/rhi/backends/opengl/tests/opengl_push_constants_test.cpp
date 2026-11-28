// OpenGL push constants against the fake driver of fake_gl.hpp: the emulation uploads the values to the next
// aligned slot of its ring buffer and binds that slot at kPushConstantBinding, only before draws that follow
// a change, and wraps around at the end of the ring. opengl_gpu_test.cpp draws with them on a real driver.

#include <teserra/rhi/device.hpp>
#include <teserra/rhi/shader.hpp>
#include <teserra/rhi/shader_blob.hpp>
#include <teserra/rhi_opengl/opengl_backend.hpp>
#include <teserra/test/assert.hpp>

#include "fake_gl.hpp"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstring>
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
layout(binding = 32, std140) uniform Draw { vec4 color; vec2 offset; } draw;
void main() { gl_Position = vec4(draw.offset, 0.0, 1.0); }
)";

constexpr std::string_view kFragmentSource = R"(#version 450 core
layout(location = 0) out vec4 fragColor;
void main() { fragColor = vec4(1.0); }
)";

struct Constants {
    std::array<f32, 4> color{};
    std::array<f32, 2> offset{};
};
static_assert(sizeof(Constants) == 24);

// A device on the fake driver (uniform buffer offset alignment 64) with pipelines whose layouts have 24 bytes
// of push constants ("Push", "Same range") and 64 bytes ("Wide").
struct PushScene {
    PushScene() {
        Result<std::unique_ptr<Device>> created =
            createDevice({.backend = Backend::OpenGL, .enableValidation = true, .enableBackendDebug = false},
                         &window, std::array{opengl::backendFactory()});
        if (!created) {
            FAIL(std::format("{}", created.error()));
        }
        device = std::move(created).value();

        const ShaderModuleHandle vertex = *device->createShaderModule(
            {.stage = ShaderStage::Vertex,
             .blob = test::glslBlob(ShaderStage::Vertex, kVertexSource, {.pushConstantSize = 24})});
        const ShaderModuleHandle fragment = *device->createShaderModule(
            {.stage = ShaderStage::Fragment, .blob = test::glslBlob(ShaderStage::Fragment, kFragmentSource)});
        const PipelineLayoutHandle layout = *device->createPipelineLayout({.pushConstants = {.size = 24}});
        const PipelineLayoutHandle wideLayout =
            *device->createPipelineLayout({.pushConstants = {.size = 64}});
        const auto create = [&](PipelineLayoutHandle pipelineLayout, std::string_view name) {
            return *device->createGraphicsPipeline({
                .vertexShader = vertex,
                .fragmentShader = fragment,
                .layout = pipelineLayout,
                .targets = {.colorFormats = {Format::Rgba8Srgb}},
                .debugName = name,
            });
        };
        pipeline = create(layout, "Push");
        sameRange = create(layout, "Same range");
        wide = create(wideLayout, "Wide");
    }

    test::FakeGlState& gl() { return window.context.state; }

    // Submits one render pass with the commands of `record`, and returns its uploads to the ring and its
    // bindings at kPushConstantBinding.
    Calls submitPass(const std::function<void(CommandList&)>& record) {
        device->beginFrame();
        CommandList& cmd = device->acquireCommandList();
        cmd.beginRenderPass({.colorAttachments = {{.texture = device->swapchain()->acquireNextImage()}}});
        record(cmd);
        cmd.endRenderPass();
        gl().calls.clear();
        device->submit(cmd);
        device->endFrame();
        if (ring == 0) {
            // The ring is created before the first upload.
            const auto storage = std::ranges::find_if(gl().calls, [](const std::string& call) {
                return call.starts_with("NamedBufferStorage(") &&
                       call.ends_with(", 65536, GL_DYNAMIC_STORAGE_BIT)");
            });
            if (storage != gl().calls.end()) {
                ring = static_cast<GLuint>(
                    std::stoul(storage->substr(std::string_view("NamedBufferStorage(").size())));
                CHECK(std::ranges::find(gl().calls, std::format("ObjectLabel(GL_BUFFER, {}, Push constants)",
                                                                ring)) != gl().calls.end());
            }
        }
        Calls pushes;
        for (const std::string& call : gl().calls) {
            if (call.starts_with(std::format("NamedBufferSubData({},", ring)) ||
                call.starts_with(
                    std::format("BindBufferRange(GL_UNIFORM_BUFFER, {},", kPushConstantBinding))) {
                pushes.push_back(call);
            }
        }
        return pushes;
    }

    // The upload and binding of one slot; the 24-byte range is bound as two 16-byte std140 rows.
    [[nodiscard]] Calls slot(u64 offset, u64 size = 32) const {
        return {std::format("NamedBufferSubData({}, {}, {})", ring, offset, size),
                std::format("BindBufferRange(GL_UNIFORM_BUFFER, 32, {}, offset {}, size {})", ring, offset,
                            size)};
    }

    [[nodiscard]] Constants uploaded(u64 offset) const {
        Constants constants{};
        std::memcpy(&constants, window.context.state.buffers.at(ring).data() + offset, sizeof(constants));
        return constants;
    }

    test::FakeGlWindow window;
    std::unique_ptr<Device> device;
    GLuint ring = 0;
    GraphicsPipelineHandle pipeline;
    GraphicsPipelineHandle sameRange;
    GraphicsPipelineHandle wide;
};

Calls operator+(Calls first, const Calls& second) {
    first.insert(first.end(), second.begin(), second.end());
    return first;
}

} // namespace

TEST_CASE(
    "OpenGL backend: push constants are uploaded to a ring buffer slot before draws that follow a change",
    "[rhi_opengl][push_constants]") {
    PushScene scene;
    // No ring until something is pushed.
    CHECK(scene.submitPass([&](CommandList& cmd) { cmd.bindPipeline(scene.pipeline); }).empty());
    CHECK(scene.ring == 0);

    const Calls calls = scene.submitPass([&](CommandList& cmd) {
        cmd.bindPipeline(scene.pipeline);
        cmd.pushConstants(Constants{.color = {1, 0, 0, 1}, .offset = {0.5F, -0.5F}});
        cmd.draw({.vertexCount = 3});
        cmd.draw({.vertexCount = 3}); // unchanged: nothing to upload
        cmd.pushConstants(std::array<f32, 2>{0.25F, 0.75F}, 16);
        cmd.draw({.vertexCount = 3}); // the next aligned slot, with the color kept
        cmd.bindPipeline(scene.sameRange);
        cmd.draw({.vertexCount = 3}); // same range: still bound
    });
    CHECK(calls == scene.slot(0) + scene.slot(64));
    CHECK(scene.uploaded(0).offset == std::array{0.5F, -0.5F});
    CHECK(scene.uploaded(64).color == std::array<f32, 4>{1, 0, 0, 1});
    CHECK(scene.uploaded(64).offset == std::array{0.25F, 0.75F});

    // The values outlive the pass in the backend, which uploads them again for a pipeline with a larger
    // range; validation makes the commands write what the shaders read first. The ring continues where it
    // stopped.
    CHECK(scene.submitPass([&](CommandList& cmd) {
        cmd.bindPipeline(scene.wide);
        cmd.pushConstants(Constants{});
        cmd.draw({.vertexCount = 3});
    }) == scene.slot(128, 64));
    // A pass without draws uploads nothing.
    CHECK(scene.submitPass([&](CommandList& cmd) { cmd.bindPipeline(scene.wide); }).empty());
}

TEST_CASE("OpenGL backend: the push-constant ring wraps around", "[rhi_opengl][push_constants]") {
    PushScene scene;

    // 65536 bytes hold 1024 slots at the fake driver's 64-byte alignment.
    const Calls calls = scene.submitPass([&](CommandList& cmd) {
        cmd.bindPipeline(scene.pipeline);
        for (u32 i = 0; i < 1025; ++i) {
            cmd.pushConstants(Constants{.color = {static_cast<f32>(i), 0, 0, 0}});
            cmd.draw({.vertexCount = 3});
        }
    });
    REQUIRE(calls.size() == usize{2} * 1025);
    CHECK(Calls(calls.end() - 4, calls.end()) == scene.slot(65536 - 64) + scene.slot(0));
    CHECK(scene.uploaded(0).color[0] == 1024.0F);
}
