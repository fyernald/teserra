// The null backend: resources, swapchain, frames and the submitted lists tests inspect, and the validation
// of descriptors and command streams against the device's objects.

#include <teserra/rhi_null/null_device.hpp>

#include <teserra/platform/window.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/test/allocation_counter.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <string>

using namespace teserra;
using namespace teserra::rhi;

namespace {

class TestWindow final : public platform::Window {
public:
    explicit TestWindow(UVec2 framebufferSize)
        : m_framebufferSize(framebufferSize) {}

    [[nodiscard]] UVec2 windowSize() const override { return m_framebufferSize; }
    [[nodiscard]] UVec2 framebufferSize() const override { return m_framebufferSize; }
    [[nodiscard]] Vec2 contentScale() const override { return Vec2{1.0F}; }
    void setSize(UVec2 /*size*/) override {}
    void setTitle(std::string_view /*title*/) override {}
    [[nodiscard]] bool shouldClose() const override { return false; }
    [[nodiscard]] bool isFocused() const override { return false; }
    [[nodiscard]] platform::NativeWindowHandle nativeHandle() const override { return {}; }
    [[nodiscard]] platform::OpenGlContext* openGlContext() override { return nullptr; }

private:
    UVec2 m_framebufferSize;
};

constexpr std::array<std::byte, 4> kShaderCode{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};

// Resources for a pipeline that renders into an Rgba8Srgb target.
struct Scene {
    explicit Scene(null::NullDevice& device)
        : device(device) {
        vertexShader = *device.createShaderModule({.stage = ShaderStage::Vertex, .blob = kShaderCode});
        fragmentShader = *device.createShaderModule({.stage = ShaderStage::Fragment, .blob = kShaderCode});
        constexpr std::array kBuffers{VertexBufferLayout{.stride = 16}};
        constexpr std::array kAttributes{VertexAttribute{.format = VertexFormat::Float4}};
        pipeline = *device.createGraphicsPipeline({
            .vertexShader = vertexShader,
            .fragmentShader = fragmentShader,
            .vertexBuffers = kBuffers,
            .vertexAttributes = kAttributes,
            .targets = {.colorFormats = {Format::Rgba8Srgb}},
            .debugName = "Sprites",
        });
        target = *device.createTexture({.width = 64,
                                        .height = 64,
                                        .usage = TextureUsage::RenderTarget | TextureUsage::CopySrc,
                                        .debugName = "Target"});
        vertices =
            *device.createBuffer({.sizeInBytes = 256, .usage = BufferUsage::Vertex, .debugName = "Vertices"});
        indices =
            *device.createBuffer({.sizeInBytes = 64, .usage = BufferUsage::Index, .debugName = "Indices"});
        staging = *device.createBuffer({
            .sizeInBytes = u64{64} * 64 * 4,
            .usage = BufferUsage::CopySrc | BufferUsage::CopyDst,
            .memory = MemoryLocation::Upload,
            .debugName = "Staging",
        });
    }

    // Records a frame that uses every resource, as a renderer would.
    void record(CommandList& cmd) const {
        cmd.pushDebugGroup("Frame");
        cmd.beginRenderPass({.colorAttachments = {{.texture = target, .loadOp = LoadOp::Clear}}});
        cmd.bindPipeline(pipeline);
        cmd.bindVertexBuffer(0, vertices);
        cmd.bindIndexBuffer(indices, IndexType::Uint16);
        cmd.drawIndexed({.indexCount = 6});
        cmd.endRenderPass();
        cmd.popDebugGroup();
        cmd.copyTextureToBuffer({.buffer = staging, .texture = target, .width = 64, .height = 64});
    }

    null::NullDevice& device;
    ShaderModuleHandle vertexShader;
    ShaderModuleHandle fragmentShader;
    GraphicsPipelineHandle pipeline;
    TextureHandle target;
    BufferHandle vertices;
    BufferHandle indices;
    BufferHandle staging;
};

template <typename Record>
void submitFrame(Device& device, Record&& record) {
    device.beginFrame();
    CommandList& commands = device.acquireCommandList();
    record(commands);
    device.submit(commands);
    device.endFrame();
}

} // namespace

TEST_CASE("Null backend: offered for Backend::Null, with or without a window", "[rhi_null]") {
    CHECK(null::backendFactory().backend == Backend::Null);

    auto headless = createDevice({.backend = Backend::Null}, nullptr, std::array{null::backendFactory()});
    REQUIRE(headless.has_value());
    CHECK((*headless)->backend() == Backend::Null);
    CHECK((*headless)->swapchain() == nullptr);

    TestWindow window({320, 200});
    auto windowed = createDevice({.backend = Backend::Null, .swapchain = {.format = Format::Bgra8Srgb}},
                                 &window, std::array{null::backendFactory()});
    REQUIRE(windowed.has_value());
    REQUIRE((*windowed)->swapchain() != nullptr);
    CHECK((*windowed)->swapchain()->size() == UVec2{320, 200});
    CHECK((*windowed)->swapchain()->format() == Format::Bgra8Srgb);
}

TEST_CASE("Null backend: resources keep copies of their descriptors", "[rhi_null]") {
    const auto device = null::createHeadlessDevice();
    CHECK(device->limits().maxTextureDimension2D == DeviceLimits{}.maxTextureDimension2D);
    BufferHandle buffer;
    ShaderModuleHandle shader;
    GraphicsPipelineHandle pipeline;
    {
        // Everything the descriptors point to goes away after creation.
        std::string name = "A name longer than any small-string buffer";
        std::vector<std::byte> code(kShaderCode.begin(), kShaderCode.end());
        std::vector buffers{VertexBufferLayout{.stride = 32}};
        std::vector attributes{VertexAttribute{.location = 2, .format = VertexFormat::Float2, .offset = 8}};
        buffer =
            *device->createBuffer({.sizeInBytes = 128, .usage = BufferUsage::Uniform, .debugName = name});
        shader = *device->createShaderModule({.stage = ShaderStage::Vertex, .blob = code, .debugName = name});
        pipeline = *device->createGraphicsPipeline({
            .vertexShader = shader,
            .vertexBuffers = buffers,
            .vertexAttributes = attributes,
            .targets = {.colorFormats = {Format::Rgba8Unorm}},
            .debugName = name,
        });
        name.assign(name.size(), '?');
        code.assign(code.size(), std::byte{0});
        buffers.clear();
        attributes.clear();
    }

    REQUIRE(device->buffer(buffer) != nullptr);
    CHECK(device->buffer(buffer)->sizeInBytes == 128);
    CHECK(device->buffer(buffer)->debugName == "A name longer than any small-string buffer");
    REQUIRE(device->shaderModule(shader) != nullptr);
    CHECK(device->shaderModule(shader)->blob.size() == 4);
    CHECK(device->shaderModule(shader)->blob[3] == std::byte{4});
    CHECK(device->shaderModule(shader)->entryPoint == "main");
    const GraphicsPipelineDesc* stored = device->graphicsPipeline(pipeline);
    REQUIRE(stored != nullptr);
    REQUIRE(stored->vertexAttributes.size() == 1);
    CHECK(stored->vertexAttributes[0].location == 2);
    CHECK(stored->vertexBuffers[0].stride == 32);
    CHECK(stored->targets.colorFormats[0] == Format::Rgba8Unorm);

    // Many more objects, so the pools grow: earlier descriptors stay where they are.
    for (int i = 0; i < 100; ++i) {
        static_cast<void>(*device->createSampler({.debugName = "Sampler"}));
    }
    CHECK(device->graphicsPipeline(pipeline) == stored);
    CHECK(device->liveObjectCount() == 103);
}

TEST_CASE("Null backend: destroy frees at once", "[rhi_null]") {
    const auto device = null::createHeadlessDevice();
    const auto texture = *device->createTexture({.width = 8, .height = 8});
    const auto sampler = *device->createSampler({});
    CHECK(device->liveObjectCount() == 2);

    device->destroy(texture);
    device->destroy(sampler);
    device->destroy(TextureHandle{}); // null handles are ignored

    CHECK(device->texture(texture) == nullptr);
    CHECK(device->sampler(sampler) == nullptr);
    CHECK(device->liveObjectCount() == 0);
#if TESERRA_ENABLE_ASSERTS
    REQUIRE_ASSERTS(device->destroy(texture));
#endif
}

TEST_CASE("Null backend: the swapchain follows the window size", "[rhi_null]") {
    TestWindow window({320, 200});
    null::NullDevice device({.backend = Backend::Null}, &window);
    auto& swapchain = static_cast<null::NullSwapchain&>(*device.swapchain());

    const TextureHandle first = swapchain.acquireNextImage();
    REQUIRE(device.texture(first) != nullptr);
    CHECK(device.texture(first)->width == 320);
    CHECK(hasAll(device.texture(first)->usage, TextureUsage::RenderTarget));
    CHECK(device.liveObjectCount() == 1);

    swapchain.resize({640, 480});
    const TextureHandle second = swapchain.acquireNextImage();
    CHECK(second != first);
    CHECK(device.texture(first) == nullptr);
    CHECK(device.texture(second)->height == 480);

    swapchain.resize({0, 0});
    CHECK(swapchain.acquireNextImage().isNull());
    CHECK(device.liveObjectCount() == 0);
#if TESERRA_ENABLE_ASSERTS
    REQUIRE_ASSERTS(swapchain.present());
#endif
    swapchain.resize({16, 16});
    swapchain.present();
    CHECK(swapchain.presentCount() == 1);
}

TEST_CASE("Null backend: submitted lists are kept for the frame", "[rhi_null]") {
    const auto device = null::createHeadlessDevice();
    const Scene scene(*device);

    device->beginFrame();
    CommandList& first = device->acquireCommandList();
    scene.record(first);
    device->submit(first);
    CommandList& second = device->acquireCommandList();
    second.pushDebugGroup("Second");
    second.popDebugGroup();
    device->submit(second);
    device->endFrame();

    REQUIRE(device->submitted().size() == 2);
    CHECK(device->submitted()[0] == &first);
    CHECK(device->submitted()[1]->commandCount() == 2);
    CHECK(device->submitted()[0]->isFinished());
    CHECK(device->frameCount() == 1);
    CHECK(
        describeCommands(*device->submitted()[0]) ==
        "PushDebugGroup \"Frame\"\n"
        "  BeginRenderPass color0=0:1 load=Clear store=Store clear=(0, 0, 0, 1)\n"
        "    BindPipeline pipeline=0:1\n"
        "    BindVertexBuffers firstSlot=0 buffers=[0:1+0]\n"
        "    BindIndexBuffer buffer=1:1+0 indexType=Uint16\n"
        "    DrawIndexed indexCount=6 instanceCount=1 firstIndex=0 vertexOffset=0 firstInstance=0\n"
        "  EndRenderPass\n"
        "PopDebugGroup\n"
        "CopyTextureToBuffer buffer=2:1+0 bytesPerRow=0 texture=0:1 mip=0 layers=0+1 region=(0, 0) 64x64\n");

    device->beginFrame(); // a new frame forgets the previous one's lists
    CHECK(device->submitted().empty());
    device->endFrame();
}

TEST_CASE("Null backend: steady-state frames do not allocate", "[rhi_null][allocation]") {
    const auto device = null::createHeadlessDevice();
    const Scene scene(*device);
    for (int frame = 0; frame < 3; ++frame) { // warm-up: both frame slots grow their lists
        submitFrame(*device, [&](CommandList& cmd) { scene.record(cmd); });
    }

    u64 allocations = 0;
    {
        const test::AllocationCounter counter;
        submitFrame(*device, [&](CommandList& cmd) { scene.record(cmd); });
        submitFrame(*device, [&](CommandList& cmd) { scene.record(cmd); });
        allocations = counter.count();
    }
    CHECK(allocations == 0);
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("Null backend: submitted streams are validated against the device's objects",
          "[rhi_null][validation]") {
    const auto device = null::createHeadlessDevice({.enableValidation = true});
    const Scene scene(*device);
    const auto failureOf = [&](auto&& record) {
        device->beginFrame();
        CommandList& commands = device->acquireCommandList();
        record(commands);
        std::string message = test::requireAssert([&] { device->submit(commands); }).message();
        device->endFrame();
        return message;
    };

    // A valid frame passes.
    submitFrame(*device, [&](CommandList& cmd) { scene.record(cmd); });

    const TextureHandle sampledOnly =
        *device->createTexture({.width = 4, .height = 4, .debugName = "Sampled"});
    CHECK(failureOf([&](CommandList& cmd) {
              cmd.beginRenderPass({.colorAttachments = {{.texture = sampledOnly}}});
              cmd.endRenderPass();
          }) == "A color attachment uses texture 'Sampled', created without TextureUsage::RenderTarget");

    const TextureHandle unorm = *device->createTexture({.format = Format::Rgba8Unorm,
                                                        .width = 4,
                                                        .height = 4,
                                                        .usage = TextureUsage::RenderTarget,
                                                        .debugName = "Unorm"});
    CHECK(failureOf([&](CommandList& cmd) {
              cmd.beginRenderPass({.colorAttachments = {{.texture = unorm}}});
              cmd.bindPipeline(scene.pipeline);
              cmd.endRenderPass();
          }) ==
          "Pipeline 'Sprites' declares Rgba8Srgb for color target 0, but the render pass has Rgba8Unorm");

    CHECK(failureOf([&](CommandList& cmd) {
              cmd.beginRenderPass({.colorAttachments = {{.texture = scene.target}}});
              cmd.bindVertexBuffer(0, scene.indices);
              cmd.endRenderPass();
          }) == "bindVertexBuffers uses buffer 'Indices', created without BufferUsage::Vertex");
    CHECK(failureOf([&](CommandList& cmd) {
              cmd.beginRenderPass({.colorAttachments = {{.texture = scene.target}}});
              cmd.bindIndexBuffer(scene.vertices, IndexType::Uint32);
              cmd.endRenderPass();
          }) == "bindIndexBuffer uses buffer 'Vertices', created without BufferUsage::Index");

    CHECK(failureOf([&](CommandList& cmd) {
              cmd.copyBuffer({.source = scene.vertices, .destination = scene.staging, .size = 16});
          }) == "copyBuffer uses buffer 'Vertices', created without BufferUsage::CopySrc");
    CHECK(failureOf([&](CommandList& cmd) {
              cmd.copyBuffer(
                  {.source = scene.staging, .sourceOffset = 16380, .destination = scene.staging, .size = 8});
          }) == "copyBuffer reads 8 bytes from offset 16380 of buffer 'Staging', which has 16384");
    CHECK(failureOf([&](CommandList& cmd) {
              cmd.copyTextureToBuffer(
                  {.buffer = scene.staging, .texture = scene.target, .x = 32, .width = 64, .height = 1});
          }) == "copyTextureToBuffer region (32, 0) 64x1, layers 0+1 is outside texture 'Target' (64x64 at "
                "mip 0, 1 layers)");
    CHECK(failureOf([&](CommandList& cmd) {
              cmd.copyTextureToBuffer({.buffer = scene.staging,
                                       .bufferOffset = 4,
                                       .texture = scene.target,
                                       .width = 64,
                                       .height = 64});
          }) == "copyTextureToBuffer needs 16384 bytes from offset 4 of buffer 'Staging', which has 16384");

    const TextureHandle stale =
        *device->createTexture({.width = 4, .height = 4, .usage = TextureUsage::RenderTarget});
    device->destroy(stale);
    CHECK(failureOf([&](CommandList& cmd) {
              cmd.beginRenderPass({.colorAttachments = {{.texture = stale}}});
              cmd.endRenderPass();
          }) == std::format("A color attachment uses texture {}, which is not alive", stale));
}

TEST_CASE("Null backend: descriptors are validated at creation", "[rhi_null][validation]") {
    const auto device = null::createHeadlessDevice({.enableValidation = true});

    REQUIRE_ASSERTS(static_cast<void>(device->createBuffer({.usage = BufferUsage::Vertex})));
    REQUIRE_ASSERTS(static_cast<void>(device->createTexture({.width = 0})));
    REQUIRE_ASSERTS(static_cast<void>(device->createShaderModule({})));
    const auto failure = test::requireAssert([&] {
        static_cast<void>(device->createGraphicsPipeline({
            .vertexShader = ShaderModuleHandle{5, 1},
            .targets = {.colorFormats = {Format::Rgba8Srgb}},
            .debugName = "Orphan",
        }));
    });
    CHECK(failure.message() == "Graphics pipeline 'Orphan': vertex shader 5:1 is not alive");
}
#endif

TEST_CASE("Null backend: without validation, anything is kept", "[rhi_null]") {
    const auto device = null::createHeadlessDevice({.enableValidation = false});
    const TextureHandle sampledOnly = *device->createTexture({.width = 4, .height = 4});

    submitFrame(*device, [&](CommandList& cmd) {
        cmd.beginRenderPass({.colorAttachments = {{.texture = sampledOnly}}});
        cmd.endRenderPass();
    });

    CHECK(device->submitted().size() == 1);
}
