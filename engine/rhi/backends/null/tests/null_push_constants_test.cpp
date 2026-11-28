// Push constants on the null backend: the shared validation of pipeline layout ranges, of shaders against
// them, and of pushConstants() and draws in submitted streams (docs/architecture/rhi.md#push-constants).

#include <teserra/rhi/device.hpp>
#include <teserra/rhi/shader_blob.hpp>
#include <teserra/rhi_null/null_device.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace teserra;
using namespace teserra::rhi;

namespace {

constexpr std::array<u32, 6> kSpirvWords{0x07230203, 0x00010500, 0x0008000B, 42, 0, 0x00020011};

// A .tshader blob whose shader reads `pushConstantSize` bytes of push constants.
std::vector<std::byte> blob(ShaderStage stage, u32 pushConstantSize) {
    return writeShaderBlob({.stage = stage,
                            .spirv = std::as_bytes(std::span(kSpirvWords)),
                            .glsl = "#version 450\nvoid main() {}\n",
                            .reflection = {.pushConstantSize = pushConstantSize}});
}

template <typename Function>
std::string failureOf(Function&& function) {
    return test::requireAssert(std::forward<Function>(function)).message();
}

// A vertex shader that reads 32 bytes of push constants and a fragment shader that reads none, and pipelines
// with a 32-byte range ("Tinted", "Other") and a 64-byte one ("Wide").
struct PushScene {
    explicit PushScene(Device& sceneDevice)
        : device(sceneDevice) {
        const std::vector<std::byte> vertexBlob = blob(ShaderStage::Vertex, 32);
        const std::vector<std::byte> fragmentBlob = blob(ShaderStage::Fragment, 0);
        vertexShader =
            *device.createShaderModule({.stage = ShaderStage::Vertex, .blob = vertexBlob, .debugName = "VS"});
        fragmentShader = *device.createShaderModule(
            {.stage = ShaderStage::Fragment, .blob = fragmentBlob, .debugName = "FS"});
        layout = *device.createPipelineLayout({.pushConstants = {.size = 32}, .debugName = "Push32"});
        wideLayout = *device.createPipelineLayout({.pushConstants = {.size = 64}, .debugName = "Push64"});
        pipeline = createPipeline(layout, "Tinted");
        otherPipeline = createPipeline(layout, "Other");
        widePipeline = createPipeline(wideLayout, "Wide");
        target = *device.createTexture({.width = 4, .height = 4, .usage = TextureUsage::RenderTarget});
    }

    [[nodiscard]] GraphicsPipelineHandle createPipeline(PipelineLayoutHandle pipelineLayout,
                                                        std::string_view name) const {
        return *device.createGraphicsPipeline({
            .vertexShader = vertexShader,
            .fragmentShader = fragmentShader,
            .layout = pipelineLayout,
            .targets = {.colorFormats = {Format::Rgba8Srgb}},
            .debugName = name,
        });
    }

    // Submits one render pass with the commands of `record`; returns the assertion message, or "" if none.
    std::string submitPass(const std::function<void(CommandList&)>& record) const {
        device.beginFrame();
        CommandList& cmd = device.acquireCommandList();
        cmd.beginRenderPass({.colorAttachments = {{.texture = target}}});
        record(cmd);
        cmd.endRenderPass();
        const auto failure = test::captureAssert([&] { device.submit(cmd); });
        device.endFrame();
        return failure ? failure->message() : "";
    }

    Device& device;
    ShaderModuleHandle vertexShader;
    ShaderModuleHandle fragmentShader;
    PipelineLayoutHandle layout;
    PipelineLayoutHandle wideLayout;
    GraphicsPipelineHandle pipeline;
    GraphicsPipelineHandle otherPipeline;
    GraphicsPipelineHandle widePipeline;
    TextureHandle target;
};

struct Constants {
    std::array<f32, 4> color{};
    std::array<f32, 4> offset{};
};
static_assert(sizeof(Constants) == 32);

} // namespace

TEST_CASE("Null backend: push-constant ranges are validated", "[rhi_null][validation]") {
    const auto device = null::createHeadlessDevice({.enableValidation = true});
    const auto create = [&](PushConstantRange range) {
        return failureOf([&] {
            static_cast<void>(device->createPipelineLayout({.pushConstants = range, .debugName = "P"}));
        });
    };

    CHECK(create({.size = 132}) ==
          "Pipeline layout 'P' has 132 bytes of push constants; the size is a multiple of 4 up to 128");
    CHECK(create({.size = 6}) ==
          "Pipeline layout 'P' has 6 bytes of push constants; the size is a multiple of 4 up to 128");
    CHECK(create({.size = 16, .visibility = ShaderStage{}}) ==
          "Pipeline layout 'P': its push constants must be visible to the vertex and/or fragment stage");
    CHECK(create({.size = 16, .visibility = ShaderStage::Compute}) ==
          "Pipeline layout 'P': its push constants must be visible to the vertex and/or fragment stage");

    // The largest range, and an empty one with no stages, are fine.
    static_cast<void>(device->createPipelineLayout({.pushConstants = {.size = kMaxPushConstantSize}}));
    static_cast<void>(
        device->createPipelineLayout({.pushConstants = {.size = 0, .visibility = ShaderStage{}}}));
}

TEST_CASE("Null backend: pipelines are validated against the push constants their shaders read",
          "[rhi_null][validation]") {
    const auto device = null::createHeadlessDevice({.enableValidation = true});
    const PushScene scene(*device);
    const auto create = [&](PushConstantRange range) {
        const PipelineLayoutHandle layout =
            *device->createPipelineLayout({.pushConstants = range, .debugName = "Layout"});
        return failureOf([&] { static_cast<void>(scene.createPipeline(layout, "Pipeline")); });
    };

    CHECK(create({.size = 16}) ==
          "Graphics pipeline 'Pipeline': vertex shader 'VS' reads 32 bytes of push constants, but layout "
          "'Layout' has 16");
    CHECK(create({.size = 32, .visibility = ShaderStage::Fragment}) ==
          "Graphics pipeline 'Pipeline': vertex shader 'VS' reads push constants, which layout 'Layout' does "
          "not make visible to the vertex stage");
    CHECK(
        failureOf([&] { static_cast<void>(scene.createPipeline({}, "Pipeline")); }) ==
        "Graphics pipeline 'Pipeline': vertex shader 'VS' reads 32 bytes of push constants, but the pipeline "
        "has no layout");
}

TEST_CASE("Null backend: draws need the push constants their shaders read", "[rhi_null][validation]") {
    const auto device = null::createHeadlessDevice({.enableValidation = true});
    const PushScene scene(*device);

    // Written whole, or in parts in any order, they last for every draw of the pass.
    CHECK(scene
              .submitPass([&](CommandList& cmd) {
                  cmd.bindPipeline(scene.pipeline);
                  cmd.pushConstants(Constants{});
                  cmd.draw({.vertexCount = 3});
                  cmd.draw({.vertexCount = 3});
              })
              .empty());
    CHECK(scene
              .submitPass([&](CommandList& cmd) {
                  cmd.bindPipeline(scene.pipeline);
                  cmd.pushConstants(std::array<f32, 4>{}, 16);
                  cmd.pushConstants(std::array<f32, 4>{}, 0);
                  cmd.draw({.vertexCount = 3});
              })
              .empty());
    // A pipeline with the same range keeps them.
    CHECK(scene
              .submitPass([&](CommandList& cmd) {
                  cmd.bindPipeline(scene.pipeline);
                  cmd.pushConstants(Constants{});
                  cmd.bindPipeline(scene.otherPipeline);
                  cmd.draw({.vertexCount = 3});
              })
              .empty());

    CHECK(scene.submitPass([&](CommandList& cmd) {
        cmd.bindPipeline(scene.pipeline);
        cmd.draw({.vertexCount = 3});
    }) == "draw with pipeline 'Tinted', whose shaders read 32 bytes of push constants, but bytes 0 to 3 were "
          "not written by pushConstants since the render pass began or a pipeline with another push-constant "
          "range was bound");
    CHECK(
        scene.submitPass([&](CommandList& cmd) {
            cmd.bindPipeline(scene.pipeline);
            cmd.pushConstants(std::array<f32, 4>{}, 0);
            cmd.draw({.vertexCount = 3});
        }) ==
        "draw with pipeline 'Tinted', whose shaders read 32 bytes of push constants, but bytes 16 to 19 were "
        "not written by pushConstants since the render pass began or a pipeline with another push-constant "
        "range was bound");
    // A pipeline with another range discards them, as Vulkan does.
    CHECK(scene
              .submitPass([&](CommandList& cmd) {
                  cmd.bindPipeline(scene.widePipeline);
                  cmd.pushConstants(Constants{});
                  cmd.bindPipeline(scene.pipeline);
                  cmd.draw({.vertexCount = 3});
              })
              .starts_with(
                  "draw with pipeline 'Tinted', whose shaders read 32 bytes of push constants, but bytes 0"));
    // So does a new render pass.
    device->beginFrame();
    CommandList& cmd = device->acquireCommandList();
    cmd.beginRenderPass({.colorAttachments = {{.texture = scene.target}}});
    cmd.bindPipeline(scene.pipeline);
    cmd.pushConstants(Constants{});
    cmd.endRenderPass();
    cmd.beginRenderPass({.colorAttachments = {{.texture = scene.target}}});
    cmd.bindPipeline(scene.pipeline);
    cmd.draw({.vertexCount = 3});
    cmd.endRenderPass();
    CHECK(failureOf([&] {
              device->submit(cmd);
          }).starts_with("draw with pipeline 'Tinted', whose shaders read 32 bytes of push constants"));
    device->endFrame();
}

TEST_CASE("Null backend: pushConstants stays within the bound pipeline's range", "[rhi_null][validation]") {
    const auto device = null::createHeadlessDevice({.enableValidation = true});
    const PushScene scene(*device);

    CHECK(scene.submitPass([&](CommandList& cmd) {
        cmd.bindPipeline(scene.pipeline);
        cmd.pushConstants(std::array<f32, 4>{}, 24);
    }) == "pushConstants writes bytes 24 to 40, but the layout of pipeline 'Tinted' has 32 bytes of push "
          "constants");
    CHECK(scene.submitPass([&](CommandList& cmd) { cmd.pushConstants(Constants{}); }) ==
          "pushConstants without a pipeline bound in this render pass");
    CHECK(scene
              .submitPass([&](CommandList& cmd) {
                  cmd.bindPipeline(scene.widePipeline);
                  cmd.pushConstants(std::array<f32, 16>{});
              })
              .empty());
}
