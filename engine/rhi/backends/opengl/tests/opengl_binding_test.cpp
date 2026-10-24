// OpenGL bind groups against the fake driver of fake_gl.hpp: each uniform buffer bound with glBindBufferRange
// at flatBinding(set, binding), dynamic offsets added in binding order, the device's offset alignment in
// validation, and storage buffers refused until #190. opengl_gpu_test.cpp draws with bind groups on a real
// driver.

#include <teserra/rhi/device.hpp>
#include <teserra/rhi_opengl/opengl_backend.hpp>
#include <teserra/test/assert.hpp>

#include "fake_gl.hpp"
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <format>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

using namespace teserra;
using namespace teserra::rhi;

namespace {

using Calls = std::vector<std::string>;

constexpr std::array kFrameEntries{BindGroupLayoutEntry{.binding = 0, .type = BindingType::UniformBuffer}};
constexpr std::array kMaterialEntries{
    BindGroupLayoutEntry{.binding = 0, .type = BindingType::UniformBuffer},
    BindGroupLayoutEntry{.binding = 1, .type = BindingType::UniformBuffer},
};
constexpr std::array kDrawEntries{
    BindGroupLayoutEntry{.binding = 0, .type = BindingType::UniformBuffer, .hasDynamicOffset = true},
    BindGroupLayoutEntry{.binding = 1, .type = BindingType::UniformBuffer, .hasDynamicOffset = true},
};

// A device on the fake driver, whose uniform buffer offset alignment is 64, with bind groups at the frame,
// material and draw sets, all in one uniform buffer.
struct BindingScene {
    explicit BindingScene(bool validation = true) {
        Result<std::unique_ptr<Device>> created = createDevice(
            {.backend = Backend::OpenGL, .enableValidation = validation, .enableBackendDebug = false},
            &window, std::array{opengl::backendFactory()});
        if (!created) {
            FAIL(std::format("{}", created.error()));
        }
        device = std::move(created).value();
        uniforms = *device->createBuffer(
            {.sizeInBytes = 1024, .usage = BufferUsage::Uniform, .debugName = "Uniforms"});
        uniformsName = gl().lastName;
        frameLayout = *device->createBindGroupLayout({.entries = kFrameEntries, .debugName = "Frame"});
        materialLayout =
            *device->createBindGroupLayout({.entries = kMaterialEntries, .debugName = "Material"});
        drawLayout = *device->createBindGroupLayout({.entries = kDrawEntries, .debugName = "Draw"});

        // Size 0: from offset 64 to the end of the buffer.
        const std::array frameResources{
            BindGroupEntry{.binding = 0, .buffer = {.buffer = uniforms, .offset = 64}}};
        frameGroup = *device->createBindGroup(
            {.layout = frameLayout, .entries = frameResources, .debugName = "Frame"});
        // Entries in any order.
        const std::array materialResources{
            BindGroupEntry{.binding = 1, .buffer = {.buffer = uniforms, .offset = 256, .size = 32}},
            BindGroupEntry{.binding = 0, .buffer = {.buffer = uniforms, .offset = 0, .size = 64}},
        };
        materialGroup = *device->createBindGroup(
            {.layout = materialLayout, .entries = materialResources, .debugName = "Material"});
        const std::array drawResources{
            BindGroupEntry{.binding = 1, .buffer = {.buffer = uniforms, .offset = 768, .size = 16}},
            BindGroupEntry{.binding = 0, .buffer = {.buffer = uniforms, .offset = 512, .size = 64}},
        };
        drawGroup =
            *device->createBindGroup({.layout = drawLayout, .entries = drawResources, .debugName = "Draw"});
    }

    test::FakeGlState& gl() { return window.context.state; }

    // Submits one render pass with the commands of `record`, and returns its glBindBufferRange calls.
    Calls submitPass(const std::function<void(CommandList&)>& record) {
        device->beginFrame();
        CommandList& cmd = device->acquireCommandList();
        cmd.beginRenderPass({.colorAttachments = {{.texture = device->swapchain()->acquireNextImage()}}});
        record(cmd);
        cmd.endRenderPass();
        gl().calls.clear();
        device->submit(cmd);
        device->endFrame();
        Calls binds;
        for (const std::string& call : gl().calls) {
            if (call.starts_with("BindBufferRange")) {
                binds.push_back(call);
            }
        }
        return binds;
    }

    [[nodiscard]] std::string bind(u32 index, u64 offset, u64 size) const {
        return std::format("BindBufferRange(GL_UNIFORM_BUFFER, {}, {}, offset {}, size {})", index,
                           uniformsName, offset, size);
    }

    test::FakeGlWindow window;
    std::unique_ptr<Device> device;
    BufferHandle uniforms;
    GLuint uniformsName = 0;
    BindGroupLayoutHandle frameLayout;
    BindGroupLayoutHandle materialLayout;
    BindGroupLayoutHandle drawLayout;
    BindGroupHandle frameGroup;
    BindGroupHandle materialGroup;
    BindGroupHandle drawGroup;
};

} // namespace

TEST_CASE("OpenGL backend: bind groups bind their uniform buffers at flatBinding(set, binding)",
          "[rhi_opengl][binding]") {
    BindingScene scene;
    CHECK(scene.device->limits().uniformBufferOffsetAlignment == 64);

    const std::array firstDraw{u32{64}, u32{128}};
    const std::array secondDraw{u32{192}, u32{0}};
    const Calls calls = scene.submitPass([&](CommandList& cmd) {
        cmd.bindGroup(kFrameGroup, scene.frameGroup);
        cmd.bindGroup(kMaterialGroup, scene.materialGroup);
        cmd.bindGroup(kDrawGroup, scene.drawGroup, firstDraw);
        cmd.bindGroup(kDrawGroup, scene.drawGroup, secondDraw);
    });

    CHECK(calls == Calls{
                       scene.bind(0, 64, 960),       // set 0: to the end of the buffer
                       scene.bind(16, 0, 64),        // set 2, binding 0
                       scene.bind(17, 256, 32),      // set 2, binding 1
                       scene.bind(24, 512 + 64, 64), // set 3: the first offset goes to binding 0
                       scene.bind(25, 768 + 128, 16),
                       scene.bind(24, 512 + 192, 64),
                       scene.bind(25, 768, 16),
                   });

    // A group can be bound at any set whose pipeline layout matches it: the set picks the GL index.
    CHECK(scene.submitPass([&](CommandList& cmd) { cmd.bindGroup(kPassGroup, scene.frameGroup); }) ==
          Calls{scene.bind(8, 64, 960)});
}

TEST_CASE("OpenGL backend: storage buffer layouts are not supported yet", "[rhi_opengl][binding]") {
    BindingScene scene;
    constexpr std::array kEntries{
        BindGroupLayoutEntry{.binding = 0, .type = BindingType::UniformBuffer},
        BindGroupLayoutEntry{.binding = 3, .type = BindingType::StorageBuffer},
    };

    const Result<BindGroupLayoutHandle> layout =
        scene.device->createBindGroupLayout({.entries = kEntries, .debugName = "Particles"});

    REQUIRE_FALSE(layout.has_value());
    CHECK(layout.error().code == ErrorCode::Unsupported);
    CHECK(layout.error().message ==
          "Bind group layout 'Particles': the OpenGL backend cannot bind storage buffers yet (binding 3)");
}

#if TESERRA_ENABLE_ASSERTS

TEST_CASE("OpenGL backend: offsets are validated against the driver's alignment",
          "[rhi_opengl][binding][validation]") {
    BindingScene scene;
    const auto failureOf = [](auto&& call) { return test::requireAssert(call).message(); };

    const std::array misaligned{
        BindGroupEntry{.binding = 0, .buffer = {.buffer = scene.uniforms, .offset = 32, .size = 64}}};
    CHECK(failureOf([&] {
              static_cast<void>(scene.device->createBindGroup(
                  {.layout = scene.frameLayout, .entries = misaligned, .debugName = "G"}));
          }) ==
          "Bind group 'G': binding 0 starts at offset 32 of buffer 'Uniforms', which is not a multiple of "
          "the device's uniformBufferOffsetAlignment (64)");

    const std::array offsets{u32{0}, u32{96}};
    CHECK(failureOf([&] {
              static_cast<void>(scene.submitPass(
                  [&](CommandList& cmd) { cmd.bindGroup(kDrawGroup, scene.drawGroup, offsets); }));
          }) == "bindGroup 'Draw' at set 3: dynamic offset 96 of binding 1 is not a multiple of the device's "
                "uniformBufferOffsetAlignment (64)");
    scene.device->endFrame();
}

TEST_CASE("OpenGL backend: without validation, replay still catches stale bind groups",
          "[rhi_opengl][binding]") {
    BindingScene scene(/*validation=*/false);
    scene.device->destroy(scene.frameGroup);

    CHECK(test::requireAssert([&] {
              static_cast<void>(
                  scene.submitPass([&](CommandList& cmd) { cmd.bindGroup(kFrameGroup, scene.frameGroup); }));
          }).message() == std::format("bindGroup uses bind group {}, which is not alive", scene.frameGroup));
    scene.device->endFrame();
}

#endif
