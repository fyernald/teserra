// Bind group layouts, pipeline layouts and bind groups on the null backend, and the shared validation that
// ties them to shader reflection, pipelines and submitted streams (docs/architecture/rhi.md#binding-model).

#include <teserra/rhi/device.hpp>
#include <teserra/rhi/shader_blob.hpp>
#include <teserra/rhi_null/null_device.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace teserra;
using namespace teserra::rhi;

namespace {

constexpr std::array<u32, 6> kSpirvWords{0x07230203, 0x00010500, 0x0008000B, 42, 0, 0x00020011};

// A .tshader blob whose reflection lists `bindings`.
std::vector<std::byte> blob(ShaderStage stage, std::vector<ShaderBinding> bindings) {
    return writeShaderBlob({.stage = stage,
                            .spirv = std::as_bytes(std::span(kSpirvWords)),
                            .glsl = "#version 450\nvoid main() {}\n",
                            .reflection = {.bindings = std::move(bindings)}});
}

// Functions, not constants: a static ShaderBinding would allocate its name before main().
ShaderBinding frameBlock() {
    return {.group = 0, .binding = 0, .type = BindingType::UniformBuffer, .size = 64, .name = "Frame"};
}
ShaderBinding drawBlock() {
    return {.group = 3, .binding = 0, .type = BindingType::UniformBuffer, .size = 64, .name = "Draw"};
}

constexpr std::array kFrameEntries{BindGroupLayoutEntry{.binding = 0, .type = BindingType::UniformBuffer}};
constexpr std::array kDrawEntries{
    BindGroupLayoutEntry{.binding = 0, .type = BindingType::UniformBuffer, .hasDynamicOffset = true}};

// A pipeline whose vertex shader uses a frame block at set 0 and a per-draw block at set 3, the layouts,
// and bind groups for both.
struct BindingScene {
    explicit BindingScene(null::NullDevice& device)
        : device(device) {
        const std::vector<std::byte> vertexBlob = blob(ShaderStage::Vertex, {frameBlock(), drawBlock()});
        vertexShader =
            *device.createShaderModule({.stage = ShaderStage::Vertex, .blob = vertexBlob, .debugName = "VS"});
        frameLayout = *device.createBindGroupLayout({.entries = kFrameEntries, .debugName = "Frame"});
        drawLayout = *device.createBindGroupLayout({.entries = kDrawEntries, .debugName = "Draw"});
        const std::array groups{frameLayout, BindGroupLayoutHandle{}, BindGroupLayoutHandle{}, drawLayout};
        layout = *device.createPipelineLayout({.groups = groups, .debugName = "Layout"});
        pipeline = *device.createGraphicsPipeline({
            .vertexShader = vertexShader,
            .layout = layout,
            .targets = {.colorFormats = {Format::Rgba8Srgb}},
            .debugName = "Sprites",
        });
        target = *device.createTexture({.width = 4, .height = 4, .usage = TextureUsage::RenderTarget});
        uniforms = *device.createBuffer(
            {.sizeInBytes = 512, .usage = BufferUsage::Uniform, .debugName = "Uniforms"});
        const std::array frameResources{
            BindGroupEntry{.binding = 0, .buffer = {.buffer = uniforms, .size = 64}}};
        frameGroup =
            *device.createBindGroup({.layout = frameLayout, .entries = frameResources, .debugName = "Frame"});
        const std::array drawResources{
            BindGroupEntry{.binding = 0, .buffer = {.buffer = uniforms, .offset = 256, .size = 64}}};
        drawGroup =
            *device.createBindGroup({.layout = drawLayout, .entries = drawResources, .debugName = "Draw"});
    }

    // A pass that binds both groups and draws.
    void record(CommandList& cmd) const {
        cmd.beginRenderPass({.colorAttachments = {{.texture = target}}});
        cmd.bindPipeline(pipeline);
        cmd.bindGroup(kFrameGroup, frameGroup);
        const std::array offsets{u32{0}};
        cmd.bindGroup(kDrawGroup, drawGroup, offsets);
        cmd.draw({.vertexCount = 3});
        cmd.endRenderPass();
    }

    null::NullDevice& device;
    ShaderModuleHandle vertexShader;
    BindGroupLayoutHandle frameLayout;
    BindGroupLayoutHandle drawLayout;
    PipelineLayoutHandle layout;
    GraphicsPipelineHandle pipeline;
    TextureHandle target;
    BufferHandle uniforms;
    BindGroupHandle frameGroup;
    BindGroupHandle drawGroup;
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

TEST_CASE("Null backend: devices report the bind group limits", "[rhi_null]") {
    const auto device = null::createHeadlessDevice();
    CHECK(device->limits().maxBindGroups == 4);
    CHECK(device->limits().maxBindingsPerGroup == 8);
    STATIC_CHECK(kFrameGroup == 0);
    STATIC_CHECK(kPassGroup == 1);
    STATIC_CHECK(kMaterialGroup == 2);
    STATIC_CHECK(kDrawGroup == 3);
}

TEST_CASE("Null backend: binding objects keep sorted copies of their descriptors", "[rhi_null]") {
    const auto device = null::createHeadlessDevice({.enableValidation = true});
    const std::array entries{
        BindGroupLayoutEntry{
            .binding = 2, .type = BindingType::SampledTexture, .visibility = ShaderStage::Fragment},
        BindGroupLayoutEntry{.binding = 0, .type = BindingType::UniformBuffer},
    };
    const BindGroupLayoutHandle material =
        *device->createBindGroupLayout({.entries = entries, .debugName = std::string("Material")});
    const BindGroupLayoutDesc* stored = device->bindGroupLayout(material);
    REQUIRE(stored != nullptr);
    CHECK(stored->debugName == "Material");
    REQUIRE(stored->entries.size() == 2);
    CHECK(stored->entries[0] == entries[1]);
    CHECK(stored->entries[1] == entries[0]);

    const std::array groups{BindGroupLayoutHandle{}, BindGroupLayoutHandle{}, material};
    const PipelineLayoutHandle layout =
        *device->createPipelineLayout({.groups = groups, .debugName = "Layout"});
    REQUIRE(device->pipelineLayout(layout) != nullptr);
    CHECK(device->pipelineLayout(layout)->groups.size() == 3);
    CHECK(device->pipelineLayout(layout)->groups[2] == material);

    const BufferHandle parameters =
        *device->createBuffer({.sizeInBytes = 64, .usage = BufferUsage::Uniform, .debugName = "Parameters"});
    const TextureHandle albedo = *device->createTexture({.width = 4, .height = 4});
    const SamplerHandle sampler = *device->createSampler({});
    const std::array resources{
        BindGroupEntry{.binding = 2, .texture = albedo, .sampler = sampler},
        BindGroupEntry{.binding = 0, .buffer = {.buffer = parameters}},
    };
    const BindGroupHandle group =
        *device->createBindGroup({.layout = material, .entries = resources, .debugName = "Brick"});
    REQUIRE(device->bindGroup(group) != nullptr);
    CHECK(device->bindGroup(group)->layout == material);
    REQUIRE(device->bindGroup(group)->entries.size() == 2);
    CHECK(device->bindGroup(group)->entries[0].buffer.buffer == parameters);
    CHECK(device->bindGroup(group)->entries[1].texture == albedo);
    CHECK(device->liveObjectCount() == 6);

    device->destroy(group);
    device->destroy(layout);
    device->destroy(material);
    CHECK(device->bindGroupLayout(material) == nullptr);
    CHECK(device->pipelineLayout(layout) == nullptr);
    CHECK(device->bindGroup(group) == nullptr);
    CHECK(device->liveObjectCount() == 3);
}

TEST_CASE("Null backend: shader modules keep the reflection of their .tshader blob", "[rhi_null]") {
    const auto device = null::createHeadlessDevice();
    const std::vector<std::byte> tshader = blob(ShaderStage::Vertex, {frameBlock()});
    const ShaderModuleHandle reflected =
        *device->createShaderModule({.stage = ShaderStage::Vertex, .blob = tshader});
    REQUIRE(device->shaderReflection(reflected) != nullptr);
    CHECK(device->shaderReflection(reflected)->bindings == std::vector{frameBlock()});

    device->destroy(reflected);
    CHECK(device->shaderReflection(reflected) == nullptr);
}

TEST_CASE("Null backend: bind groups survive their layouts", "[rhi_null][validation]") {
    const auto device = null::createHeadlessDevice({.enableValidation = true});
    const BindingScene scene(*device);

    // Pipelines, pipeline layouts and bind groups copied what they need from the layouts.
    device->destroy(scene.frameLayout);
    device->destroy(scene.drawLayout);
    device->destroy(scene.layout);
    submitFrame(*device, [&](CommandList& cmd) { scene.record(cmd); });
    CHECK(device->submitted().size() == 1);
}

#if TESERRA_ENABLE_ASSERTS

namespace {

template <typename Function>
std::string failureOf(Function&& function) {
    return test::requireAssert(std::forward<Function>(function)).message();
}

} // namespace

TEST_CASE("Null backend: bind group layouts are validated", "[rhi_null][validation]") {
    const auto device = null::createHeadlessDevice({.enableValidation = true});
    const auto create = [&](std::span<const BindGroupLayoutEntry> entries) {
        return failureOf([&] {
            static_cast<void>(device->createBindGroupLayout({.entries = entries, .debugName = "L"}));
        });
    };

    std::array<BindGroupLayoutEntry, 9> tooMany{};
    for (u32 i = 0; i < tooMany.size(); ++i) {
        tooMany[i].binding = i;
    }
    CHECK(create(tooMany) == "Bind group layout 'L' has 9 entries; the limit is 8");
    CHECK(create(std::array{BindGroupLayoutEntry{.binding = 8}}) ==
          "Bind group layout 'L' has binding 8; bindings are 0 to 7");
    CHECK(create(std::array{BindGroupLayoutEntry{.visibility = ShaderStage::None}}) ==
          "Bind group layout 'L': binding 0 must be visible to the vertex and/or fragment stage");
    CHECK(
        create(std::array{BindGroupLayoutEntry{.visibility = ShaderStage::Vertex | ShaderStage::Compute}}) ==
        "Bind group layout 'L': binding 0 must be visible to the vertex and/or fragment stage");
    CHECK(create(std::array{BindGroupLayoutEntry{
              .binding = 1, .type = BindingType::StorageBuffer, .hasDynamicOffset = true}}) ==
          "Bind group layout 'L': binding 1 is a storage buffer with a dynamic offset; only uniform buffers "
          "have "
          "them");
    CHECK(create(std::array{BindGroupLayoutEntry{.binding = 3}, BindGroupLayoutEntry{.binding = 3}}) ==
          "Bind group layout 'L' has two entries for binding 3");
}

TEST_CASE("Null backend: pipeline layouts are validated", "[rhi_null][validation]") {
    const auto device = null::createHeadlessDevice({.enableValidation = true});
    const BindGroupLayoutHandle layout = *device->createBindGroupLayout({.entries = kFrameEntries});

    const std::array<BindGroupLayoutHandle, 5> five{layout, layout, layout, layout, layout};
    CHECK(failureOf([&] {
              static_cast<void>(device->createPipelineLayout({.groups = five, .debugName = "P"}));
          }) == "Pipeline layout 'P' has 5 groups; the limit is 4");

    const BindGroupLayoutHandle stale = *device->createBindGroupLayout({.entries = kFrameEntries});
    device->destroy(stale);
    const std::array groups{layout, stale};
    CHECK(failureOf([&] {
              static_cast<void>(device->createPipelineLayout({.groups = groups, .debugName = "P"}));
          }) == std::format("Pipeline layout 'P': the layout of set 1 ({}) is not alive", stale));
}

TEST_CASE("Null backend: bind groups are validated against their layout", "[rhi_null][validation]") {
    const auto device = null::createHeadlessDevice({.enableValidation = true});
    const BindGroupLayoutHandle frame =
        *device->createBindGroupLayout({.entries = kFrameEntries, .debugName = "Frame"});
    const BindGroupLayoutHandle draw =
        *device->createBindGroupLayout({.entries = kDrawEntries, .debugName = "Draw"});
    const BufferHandle uniforms =
        *device->createBuffer({.sizeInBytes = 512, .usage = BufferUsage::Uniform, .debugName = "Uniforms"});
    const BufferHandle vertices =
        *device->createBuffer({.sizeInBytes = 512, .usage = BufferUsage::Vertex, .debugName = "Vertices"});
    const auto create = [&](BindGroupLayoutHandle layout, std::span<const BindGroupEntry> entries) {
        return failureOf([&] {
            static_cast<void>(
                device->createBindGroup({.layout = layout, .entries = entries, .debugName = "G"}));
        });
    };
    const auto one = [](BindGroupEntry entry) { return std::array{entry}; };

    const BindGroupLayoutHandle staleLayout = *device->createBindGroupLayout({.entries = kFrameEntries});
    device->destroy(staleLayout);
    CHECK(create(staleLayout, one({.buffer = {.buffer = uniforms}})) ==
          std::format("Bind group 'G': its layout {} is not alive", staleLayout));
    CHECK(create(frame, {}) == "Bind group 'G' has 0 entries for the 1 binding(s) of layout 'Frame'");
    CHECK(create(frame, one({.binding = 5, .buffer = {.buffer = uniforms}})) ==
          "Bind group 'G' has an entry for binding 5, which layout 'Frame' lacks");

    const BufferHandle staleBuffer =
        *device->createBuffer({.sizeInBytes = 16, .usage = BufferUsage::Uniform});
    device->destroy(staleBuffer);
    CHECK(create(frame, one({.buffer = {.buffer = staleBuffer}})) ==
          std::format("Bind group 'G': binding 0 needs a live buffer, not {}", staleBuffer));
    CHECK(create(frame, one({.buffer = {.buffer = vertices}})) ==
          "Bind group 'G': buffer 'Vertices' at binding 0 was created without BufferUsage::Uniform");
    CHECK(create(frame, one({.buffer = {.buffer = uniforms, .offset = 480, .size = 64}})) ==
          "Bind group 'G': binding 0 sees 64 bytes from offset 480 of buffer 'Uniforms', which has 512");
    CHECK(create(frame, one({.buffer = {.buffer = uniforms, .offset = 512}})) ==
          "Bind group 'G': binding 0 sees 0 bytes from offset 512 of buffer 'Uniforms', which has 512");
    CHECK(create(draw, one({.buffer = {.buffer = uniforms}})) ==
          "Bind group 'G': binding 0 has a dynamic offset, so its buffer range needs a size");

    constexpr std::array kTextureEntries{BindGroupLayoutEntry{.type = BindingType::SampledTexture}};
    const BindGroupLayoutHandle textured = *device->createBindGroupLayout({.entries = kTextureEntries});
    const TextureHandle target = *device->createTexture(
        {.width = 4, .height = 4, .usage = TextureUsage::RenderTarget, .debugName = "Target"});
    const TextureHandle albedo = *device->createTexture({.width = 4, .height = 4});
    const SamplerHandle sampler = *device->createSampler({});
    CHECK(create(textured, one({.texture = target, .sampler = sampler})) ==
          "Bind group 'G': texture 'Target' at binding 0 was created without TextureUsage::Sampled");
    CHECK(create(textured, one({.texture = albedo})) ==
          std::format("Bind group 'G': binding 0 needs a live sampler, not {}", SamplerHandle{}));
}

TEST_CASE("Null backend: pipelines are validated against their shaders' reflection",
          "[rhi_null][validation]") {
    const auto device = null::createHeadlessDevice({.enableValidation = true});
    const auto shader = [&](ShaderStage stage, std::vector<ShaderBinding> bindings, const char* name) {
        const std::vector<std::byte> code = blob(stage, std::move(bindings));
        return *device->createShaderModule({.stage = stage, .blob = code, .debugName = name});
    };
    const ShaderModuleHandle vertex = shader(ShaderStage::Vertex, {frameBlock()}, "VS");
    const BindGroupLayoutHandle frame =
        *device->createBindGroupLayout({.entries = kFrameEntries, .debugName = "Frame"});
    const auto pipelineLayout = [&](std::span<const BindGroupLayoutHandle> groups) {
        return *device->createPipelineLayout({.groups = groups, .debugName = "Layout"});
    };
    const auto create = [&](ShaderModuleHandle vs, ShaderModuleHandle fs, PipelineLayoutHandle layout) {
        return failureOf([&] {
            static_cast<void>(
                device->createGraphicsPipeline({.vertexShader = vs,
                                                .fragmentShader = fs,
                                                .layout = layout,
                                                .targets = {.colorFormats = {Format::Rgba8Srgb}},
                                                .debugName = "P"}));
        });
    };

    // A layout with the binding passes; so does a shader without bindings and no layout.
    const PipelineLayoutHandle withFrame = pipelineLayout(std::array{frame});
    CHECK(device
              ->createGraphicsPipeline({.vertexShader = vertex,
                                        .layout = withFrame,
                                        .targets = {.colorFormats = {Format::Rgba8Srgb}}})
              .has_value());
    const ShaderModuleHandle unbound = shader(ShaderStage::Vertex, {}, "Unbound");
    CHECK(device
              ->createGraphicsPipeline(
                  {.vertexShader = unbound, .targets = {.colorFormats = {Format::Rgba8Srgb}}})
              .has_value());

    CHECK(create(vertex, {}, {}) ==
          "Graphics pipeline 'P': vertex shader 'VS' uses uniform buffer 'Frame' at (group 0, "
          "binding 0), but the pipeline has no layout");
    const PipelineLayoutHandle frameAtSet1 = pipelineLayout(std::array{BindGroupLayoutHandle{}, frame});
    CHECK(create(vertex, {}, frameAtSet1) ==
          "Graphics pipeline 'P': vertex shader 'VS' uses uniform buffer 'Frame' at "
          "(group 0, binding 0), which layout 'Layout' does not have");

    const ShaderModuleHandle textured =
        shader(ShaderStage::Fragment,
               {{.group = 0, .binding = 0, .type = BindingType::SampledTexture, .name = "albedo"}}, "FS");
    CHECK(create(vertex, textured, withFrame) == "Graphics pipeline 'P': fragment shader 'FS' uses 'albedo' "
                                                 "at (group 0, binding 0) as a sampled texture, "
                                                 "but layout 'Layout' has a uniform buffer there");

    constexpr std::array kVertexOnly{BindGroupLayoutEntry{.visibility = ShaderStage::Vertex}};
    const BindGroupLayoutHandle vertexOnly = *device->createBindGroupLayout({.entries = kVertexOnly});
    const ShaderModuleHandle fragment = shader(ShaderStage::Fragment, {frameBlock()}, "FS");
    CHECK(create(vertex, fragment, pipelineLayout(std::array{vertexOnly})) ==
          "Graphics pipeline 'P': fragment shader 'FS' uses 'Frame' at (group 0, binding 0), which layout "
          "'Layout' "
          "does not make visible to the fragment stage");

    const PipelineLayoutHandle stale = pipelineLayout(std::array{frame});
    device->destroy(stale);
    CHECK(create(vertex, {}, stale) ==
          std::format("Graphics pipeline 'P': its layout {} is not alive", stale));
}

TEST_CASE("Null backend: draws need compatible bind groups at the pipeline's sets",
          "[rhi_null][validation]") {
    const auto device = null::createHeadlessDevice({.enableValidation = true});
    const BindingScene scene(*device);
    const std::array offsets{u32{0}};
    // The message of the assertion that submitting `record`'s commands triggers.
    const auto failureOfFrame = [&](auto&& record) {
        device->beginFrame();
        CommandList& commands = device->acquireCommandList();
        record(commands);
        std::string message = failureOf([&] { device->submit(commands); });
        device->endFrame();
        return message;
    };

    submitFrame(*device, [&](CommandList& cmd) { scene.record(cmd); });

    CHECK(failureOfFrame([&](CommandList& cmd) {
              cmd.beginRenderPass({.colorAttachments = {{.texture = scene.target}}});
              cmd.bindPipeline(scene.pipeline);
              cmd.bindGroup(kDrawGroup, scene.drawGroup, offsets);
              cmd.draw({.vertexCount = 3});
              cmd.endRenderPass();
          }) == "draw needs a bind group at set 0 for pipeline 'Sprites', and none is bound");

    // The draw group's layout at the frame set.
    CHECK(failureOfFrame([&](CommandList& cmd) {
              cmd.beginRenderPass({.colorAttachments = {{.texture = scene.target}}});
              cmd.bindPipeline(scene.pipeline);
              cmd.bindGroup(kFrameGroup, scene.drawGroup, offsets);
              cmd.bindGroup(kDrawGroup, scene.drawGroup, offsets);
              cmd.draw({.vertexCount = 3});
              cmd.endRenderPass();
          }) ==
          "draw with bind group 'Draw' at set 0, whose layout differs from that set of pipeline 'Sprites' "
          "(layout 'Layout')");

    CHECK(failureOfFrame([&](CommandList& cmd) {
              cmd.beginRenderPass({.colorAttachments = {{.texture = scene.target}}});
              cmd.bindGroup(kDrawGroup, scene.drawGroup);
              cmd.endRenderPass();
          }) == "bindGroup 'Draw' at set 3 has 0 dynamic offsets for the 1 dynamic binding(s) of its layout");

    // Groups stay bound across pipelines but not across passes.
    CHECK(failureOfFrame([&](CommandList& cmd) {
              cmd.beginRenderPass({.colorAttachments = {{.texture = scene.target}}});
              cmd.bindGroup(kFrameGroup, scene.frameGroup);
              cmd.bindGroup(kDrawGroup, scene.drawGroup, offsets);
              cmd.endRenderPass();
              cmd.beginRenderPass({.colorAttachments = {{.texture = scene.target}}});
              cmd.bindPipeline(scene.pipeline);
              cmd.draw({.vertexCount = 3});
              cmd.endRenderPass();
          }) == "draw needs a bind group at set 0 for pipeline 'Sprites', and none is bound");
    submitFrame(*device, [&](CommandList& cmd) {
        cmd.beginRenderPass({.colorAttachments = {{.texture = scene.target}}});
        cmd.bindGroup(kFrameGroup, scene.frameGroup);
        cmd.bindGroup(kDrawGroup, scene.drawGroup, offsets);
        cmd.bindPipeline(scene.pipeline);
        cmd.draw({.vertexCount = 3});
        cmd.bindPipeline(scene.pipeline);
        cmd.draw({.vertexCount = 3});
        cmd.endRenderPass();
    });

    // A separately created layout with equal entries is compatible.
    const BindGroupLayoutHandle twin =
        *device->createBindGroupLayout({.entries = kFrameEntries, .debugName = "Twin"});
    const std::array twinResources{
        BindGroupEntry{.binding = 0, .buffer = {.buffer = scene.uniforms, .size = 64}}};
    const BindGroupHandle twinGroup = *device->createBindGroup({.layout = twin, .entries = twinResources});
    submitFrame(*device, [&](CommandList& cmd) {
        cmd.beginRenderPass({.colorAttachments = {{.texture = scene.target}}});
        cmd.bindPipeline(scene.pipeline);
        cmd.bindGroup(kFrameGroup, twinGroup);
        cmd.bindGroup(kDrawGroup, scene.drawGroup, offsets);
        cmd.draw({.vertexCount = 3});
        cmd.endRenderPass();
    });

    // Stale groups and resources destroyed while a group uses them.
    const BindGroupHandle stale = *device->createBindGroup({.layout = twin, .entries = twinResources});
    device->destroy(stale);
    CHECK(failureOfFrame([&](CommandList& cmd) {
              cmd.beginRenderPass({.colorAttachments = {{.texture = scene.target}}});
              cmd.bindGroup(kFrameGroup, stale);
              cmd.endRenderPass();
          }) == std::format("bindGroup uses bind group {}, which is not alive", stale));
    device->destroy(scene.uniforms);
    CHECK(failureOfFrame([&](CommandList& cmd) {
              cmd.beginRenderPass({.colorAttachments = {{.texture = scene.target}}});
              cmd.bindGroup(kFrameGroup, scene.frameGroup);
              cmd.endRenderPass();
          }) ==
          std::format("bindGroup 'Frame' uses buffer {} at binding 0, which is not alive", scene.uniforms));
}

TEST_CASE("Null backend: uniform buffer offsets follow the device's alignment and range limits",
          "[rhi_null][validation]") {
    const auto device = null::createHeadlessDevice({.enableValidation = true});
    const BindingScene scene(*device);
    const DeviceLimits& limits = device->limits();
    REQUIRE(limits.uniformBufferOffsetAlignment == 256);
    REQUIRE(limits.maxUniformBufferRange == 16384);
    const auto failureOf = [](auto&& call) { return test::requireAssert(call).message(); };

    // At creation: the offset of every uniform buffer range, and the bytes it sees.
    const BufferHandle large = *device->createBuffer(
        {.sizeInBytes = 32768, .usage = BufferUsage::Uniform | BufferUsage::Storage, .debugName = "Large"});
    const auto create = [&](BindGroupLayoutHandle layout, BufferBinding range) {
        const std::array entries{BindGroupEntry{.binding = 0, .buffer = range}};
        return failureOf([&] {
            static_cast<void>(
                device->createBindGroup({.layout = layout, .entries = entries, .debugName = "G"}));
        });
    };
    CHECK(create(scene.frameLayout, {.buffer = large, .offset = 128, .size = 64}) ==
          "Bind group 'G': binding 0 starts at offset 128 of buffer 'Large', which is not a multiple of the "
          "device's uniformBufferOffsetAlignment (256)");
    CHECK(create(scene.frameLayout, {.buffer = large, .offset = 256}) ==
          "Bind group 'G': binding 0 sees 32512 bytes of buffer 'Large', more than the device's "
          "maxUniformBufferRange (16384)");
    const std::array fits{BindGroupEntry{.binding = 0, .buffer = {.buffer = large, .offset = 16384}}};
    CHECK_FALSE(test::captureAssert(
        [&] { device->destroy(*device->createBindGroup({.layout = scene.frameLayout, .entries = fits})); }));
    // Storage buffers have neither limit.
    constexpr std::array kStorageEntries{
        BindGroupLayoutEntry{.binding = 0, .type = BindingType::StorageBuffer}};
    const BindGroupLayoutHandle storage = *device->createBindGroupLayout({.entries = kStorageEntries});
    const std::array storageResources{
        BindGroupEntry{.binding = 0, .buffer = {.buffer = large, .offset = 16}}};
    CHECK_FALSE(test::captureAssert([&] {
        device->destroy(*device->createBindGroup({.layout = storage, .entries = storageResources}));
    }));

    // At submit: each dynamic offset, and the window it moves. The draw group sees 64 bytes from offset 256
    // of a 512-byte buffer.
    const auto failureOfBind = [&](u32 offset) {
        device->beginFrame();
        CommandList& cmd = device->acquireCommandList();
        cmd.beginRenderPass({.colorAttachments = {{.texture = scene.target}}});
        const std::array offsets{offset};
        cmd.bindGroup(kDrawGroup, scene.drawGroup, offsets);
        cmd.endRenderPass();
        std::optional<test::AssertionFailure> failure = test::captureAssert([&] { device->submit(cmd); });
        device->endFrame();
        return failure ? failure->message() : std::string();
    };
    CHECK(failureOfBind(0).empty());
    CHECK(failureOfBind(64) ==
          "bindGroup 'Draw' at set 3: dynamic offset 64 of binding 0 is not a multiple of "
          "the device's uniformBufferOffsetAlignment (256)");
    CHECK(failureOfBind(256) ==
          "bindGroup 'Draw' at set 3: dynamic offset 256 moves the 64 bytes of binding 0 "
          "from offset 256 past the end of buffer 'Uniforms' (512 bytes)");
}

#endif
