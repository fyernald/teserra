// The null backend: resources, swapchain, frames and the submitted lists tests inspect, and the validation
// of descriptors and command streams against the device's objects.

#include <teserra/rhi_null/null_device.hpp>

#include <teserra/platform/window.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/shader_blob.hpp>
#include <teserra/test/allocation_counter.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <vector>

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

constexpr std::array<u32, 2> kSpirvWords{0x07230203, 0x00010500};

// A .tshader blob of `stage`; the null backend reads only its stage and reflection.
std::vector<std::byte> shaderBlob(ShaderStage stage) {
    return writeShaderBlob({.stage = stage, .spirv = std::as_bytes(std::span(kSpirvWords))});
}

std::vector<std::byte> bytes(std::initializer_list<u8> values) {
    std::vector<std::byte> result;
    for (const u8 value : values) {
        result.push_back(std::byte{value});
    }
    return result;
}

// Resources for a pipeline that renders into an Rgba8Srgb target.
struct Scene {
    explicit Scene(null::NullDevice& device)
        : device(device) {
        const std::vector<std::byte> vertexBlob = shaderBlob(ShaderStage::Vertex);
        const std::vector<std::byte> fragmentBlob = shaderBlob(ShaderStage::Fragment);
        vertexShader = *device.createShaderModule({.stage = ShaderStage::Vertex, .blob = vertexBlob});
        fragmentShader = *device.createShaderModule({.stage = ShaderStage::Fragment, .blob = fragmentBlob});
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
        std::vector<std::byte> code = shaderBlob(ShaderStage::Vertex);
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
    CHECK(std::ranges::equal(device->shaderModule(shader)->blob, shaderBlob(ShaderStage::Vertex)));
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

TEST_CASE("Null backend: buffers have memory that writeBuffer and map() reach", "[rhi_null]") {
    const auto device = null::createHeadlessDevice({.enableValidation = true});
    const BufferHandle vertices = *device->createBuffer(
        {.sizeInBytes = 8, .usage = BufferUsage::Vertex | BufferUsage::CopyDst, .debugName = "Vertices"});
    const BufferHandle frameData = *device->createBuffer(
        {.sizeInBytes = 4, .usage = BufferUsage::Uniform, .memory = MemoryLocation::Upload});
    CHECK(std::ranges::equal(device->bufferData(vertices), bytes({0, 0, 0, 0, 0, 0, 0, 0})));

    device->writeBuffer(vertices, 4, bytes({1, 2, 3}));
    device->writeBuffer(vertices, 8, {}); // empty data does nothing, even at the end
    CHECK(std::ranges::equal(device->bufferData(vertices), bytes({0, 0, 0, 0, 1, 2, 3, 0})));

    const std::span<std::byte> mapped = device->map(frameData);
    CHECK(mapped.size() == 4);
    CHECK(device->map(frameData).data() == mapped.data()); // mapped for the buffer's lifetime
    mapped[2] = std::byte{7};
    CHECK(std::ranges::equal(device->bufferData(frameData), bytes({0, 0, 7, 0})));

    device->destroy(vertices);
    CHECK(device->bufferData(vertices).empty());
}

TEST_CASE("Null backend: writeTexture stores texels top row first, per layer and mip", "[rhi_null]") {
    const auto device = null::createHeadlessDevice({.enableValidation = true});

    SECTION("a region with padded rows") {
        const TextureHandle texture =
            *device->createTexture({.format = Format::R8Unorm, .width = 4, .height = 3});
        CHECK(device->textureData(texture).empty()); // nothing written yet

        // 2x2 texels at (1, 1), rows 3 bytes apart; the third byte of each row is padding.
        device->writeTexture(texture, {.x = 1, .y = 1, .width = 2, .height = 2}, bytes({1, 2, 99, 3, 4}), 3);

        CHECK(std::ranges::equal(device->textureData(texture), bytes({
                                                                   0,
                                                                   0,
                                                                   0,
                                                                   0, // row 0 (top)
                                                                   0,
                                                                   1,
                                                                   2,
                                                                   0, // row 1
                                                                   0,
                                                                   3,
                                                                   4,
                                                                   0, // row 2
                                                               })));
    }
    SECTION("layers of an array texture and a mip level") {
        const TextureHandle texture = *device->createTexture({.dimension = TextureDimension::Tex2DArray,
                                                              .format = Format::Rg8Unorm,
                                                              .width = 2,
                                                              .height = 2,
                                                              .depthOrLayers = 3,
                                                              .mipLevels = 2});

        // Mip 1 is 1x1: one texel of 2 bytes in layers 1 and 2.
        device->writeTexture(texture,
                             {.mipLevel = 1, .arrayLayer = 1, .layerCount = 2, .width = 1, .height = 1},
                             bytes({5, 6, 7, 8}));

        CHECK(device->textureData(texture, 0).empty());
        CHECK(std::ranges::equal(device->textureData(texture, 1), bytes({0, 0, 5, 6, 7, 8})));
        CHECK(device->textureData(texture, 2).empty()); // no such level
        device->generateMipmaps(texture);               // validated, not computed
        CHECK(device->textureData(texture, 0).empty());
    }
}

TEST_CASE("Null backend: shader modules need a .tshader blob of their stage", "[rhi_null]") {
    const auto device = null::createHeadlessDevice();

    constexpr std::array kOpaque{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    const Result<ShaderModuleHandle> opaque =
        device->createShaderModule({.blob = kOpaque, .debugName = "Opaque"});
    REQUIRE_FALSE(opaque.has_value());
    CHECK(opaque.error().code == ErrorCode::InvalidData);
    CHECK(opaque.error().message ==
          "Shader module 'Opaque': The shader blob is 4 bytes, too short for a .tshader blob");

    const std::vector<std::byte> fragment = shaderBlob(ShaderStage::Fragment);
    const Result<ShaderModuleHandle> swapped =
        device->createShaderModule({.stage = ShaderStage::Vertex, .blob = fragment, .debugName = "Swapped"});
    REQUIRE_FALSE(swapped.has_value());
    CHECK(swapped.error().code == ErrorCode::InvalidArgument);
    CHECK(swapped.error().message ==
          "Shader module 'Swapped' is a vertex shader, but its blob holds a fragment shader");
    CHECK(device->liveObjectCount() == 0);
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("Null backend: writeBuffer and map() are validated", "[rhi_null][validation]") {
    const auto device = null::createHeadlessDevice({.enableValidation = true});
    const BufferHandle vertices = *device->createBuffer(
        {.sizeInBytes = 8, .usage = BufferUsage::Vertex | BufferUsage::CopyDst, .debugName = "Vertices"});
    const BufferHandle immutable =
        *device->createBuffer({.sizeInBytes = 8, .usage = BufferUsage::Vertex, .debugName = "Static"});
    const BufferHandle frameData =
        *device->createBuffer({.sizeInBytes = 8,
                               .usage = BufferUsage::Uniform | BufferUsage::CopyDst,
                               .memory = MemoryLocation::Upload,
                               .debugName = "Frame data"});
    const std::vector<std::byte> data = bytes({1, 2, 3, 4});
    const auto failureOf = [](auto&& call) { return test::requireAssert(call).message(); };

    CHECK(failureOf([&] { device->writeBuffer(vertices, 6, data); }) ==
          "writeBuffer of 4 bytes at offset 6 is outside buffer 'Vertices' (8 bytes)");
    CHECK(failureOf([&] { device->writeBuffer(vertices, ~u64{0}, data); }) ==
          std::format("writeBuffer of 4 bytes at offset {} is outside buffer 'Vertices' (8 bytes)", ~u64{0}));
    CHECK(failureOf([&] { device->writeBuffer(immutable, 0, data); }) ==
          "writeBuffer to buffer 'Static', created without BufferUsage::CopyDst");
    CHECK(failureOf([&] { device->writeBuffer(frameData, 0, data); }) ==
          "writeBuffer to buffer 'Frame data', which is not GpuOnly; write Upload buffers through map()");
    CHECK(failureOf([&] { static_cast<void>(device->map(vertices)); }) ==
          "map() of buffer 'Vertices', which is GpuOnly; only Upload and Readback buffers are mapped");

    device->destroy(frameData);
    CHECK(failureOf([&] { device->writeBuffer(frameData, 0, data); }) ==
          std::format("writeBuffer to buffer {}, which is not alive", frameData));
    CHECK(failureOf([&] { static_cast<void>(device->map(frameData)); }) ==
          std::format("map() of buffer {}, which is not alive", frameData));
}

TEST_CASE("Null backend: writeTexture and generateMipmaps are validated", "[rhi_null][validation]") {
    const auto device = null::createHeadlessDevice({.enableValidation = true});
    const TextureHandle atlas =
        *device->createTexture({.format = Format::Rgba8Srgb, .width = 4, .height = 4, .debugName = "Atlas"});
    const auto failureOf = [](auto&& call) { return test::requireAssert(call).message(); };

    CHECK(failureOf(
              [&] { device->writeTexture(atlas, {.x = 2, .width = 4, .height = 1}, bytes({1, 2, 3}), 0); }) ==
          "writeTexture region (2, 0) 4x1, layers 0+1 is outside texture 'Atlas' (4x4 at mip 0, 1 layers)");
    CHECK(failureOf([&] { device->generateMipmaps(atlas); }) ==
          "generateMipmaps of texture 'Atlas', which has one mip level");
    device->destroy(atlas);
    CHECK(failureOf([&] { device->writeTexture(atlas, {.width = 1, .height = 1}, bytes({1, 2, 3, 4})); }) ==
          std::format("writeTexture to texture {}, which is not alive", atlas));
}
#endif

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

TEST_CASE("Null backend: draws need the pipeline's vertex buffers bound in the pass",
          "[rhi_null][validation]") {
    const auto device = null::createHeadlessDevice({.enableValidation = true});
    const Scene scene(*device);
    const auto record = [&](bool bindVertices) {
        return [&scene, bindVertices](CommandList& cmd) {
            cmd.beginRenderPass({.colorAttachments = {{.texture = scene.target}}});
            cmd.bindPipeline(scene.pipeline);
            if (bindVertices) {
                cmd.bindVertexBuffer(0, scene.vertices);
            }
            cmd.draw({.vertexCount = 3});
            cmd.endRenderPass();
        };
    };

    submitFrame(*device, record(true));
    CHECK(test::requireAssert([&] { submitFrame(*device, record(false)); }).message() ==
          "draw needs vertex buffer 0 of pipeline 'Sprites', which is not bound");
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
