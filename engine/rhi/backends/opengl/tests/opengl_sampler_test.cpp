// OpenGL samplers and texture bindings against the fake driver of fake_gl.hpp: every sampler parameter set
// explicitly, anisotropy only with the extension and clamped to its limit, and bind groups that bind each
// texture with its sampler to the texture unit at flatBinding(set, binding). opengl_gpu_test.cpp samples
// textures on a real driver.

#include <teserra/rhi/device.hpp>
#include <teserra/rhi_opengl/opengl_backend.hpp>
#include <teserra/test/assert.hpp>

#include "fake_gl.hpp"
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <format>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace teserra;
using namespace teserra::rhi;

namespace {

using Calls = std::vector<std::string>;

std::unique_ptr<Device> requireOpenGl(test::FakeGlWindow& window, bool validation = true) {
    Result<std::unique_ptr<Device>> device = createDevice(
        {.backend = Backend::OpenGL, .enableValidation = validation, .enableBackendDebug = false}, &window,
        std::array{opengl::backendFactory()});
    if (!device) {
        FAIL(std::format("{}", device.error()));
    }
    return std::move(device).value();
}

// The calls of creating a sampler from `desc` on `device`, and the sampler's GL name.
struct CreatedSampler {
    SamplerHandle handle;
    GLuint name = 0;
    Calls calls;
};

CreatedSampler createSampler(Device& device, test::FakeGlState& gl, const SamplerDesc& desc) {
    gl.calls.clear();
    const Result<SamplerHandle> sampler = device.createSampler(desc);
    REQUIRE(sampler.has_value());
    return {.handle = *sampler, .name = gl.lastName, .calls = gl.calls};
}

// The creation calls of the sampler `name` up to its parameters, given as pairs such as
// {"GL_TEXTURE_MAG_FILTER", "GL_LINEAR"}.
Calls creation(GLuint name, const std::vector<std::pair<std::string, std::string>>& values) {
    Calls calls{std::format("CreateSamplers() = {}", name)};
    for (const auto& [parameter, value] : values) {
        calls.push_back(std::format("SamplerParameteri({}, {}, {})", name, parameter, value));
    }
    return calls;
}

// A device with a 4x4 Rgba8Srgb texture, a sampler and a material group (set 2) that binds them at binding 1
// after a uniform buffer at binding 0.
struct TextureScene {
    explicit TextureScene(bool validation = true)
        : device(requireOpenGl(window, validation)) {
        texture = *device->createTexture({.width = 4, .height = 4, .debugName = "Albedo"});
        textureName = gl().lastName;
        sampler = *device->createSampler({.debugName = "Linear"});
        samplerName = gl().lastName;
        uniforms = *device->createBuffer({.sizeInBytes = 256, .usage = BufferUsage::Uniform});
        uniformsName = gl().lastName;
        constexpr std::array kEntries{
            BindGroupLayoutEntry{.binding = 0, .type = BindingType::UniformBuffer},
            BindGroupLayoutEntry{
                .binding = 1, .type = BindingType::SampledTexture, .visibility = ShaderStage::Fragment},
        };
        layout = *device->createBindGroupLayout({.entries = kEntries, .debugName = "Material"});
        const std::array resources{
            BindGroupEntry{.binding = 1, .texture = texture, .sampler = sampler},
            BindGroupEntry{.binding = 0, .buffer = {.buffer = uniforms, .size = 64}},
        };
        group = *device->createBindGroup({.layout = layout, .entries = resources, .debugName = "Material"});
    }

    test::FakeGlState& gl() { return window.context.state; }

    // Submits one render pass that binds the material group at `set`, and returns the binding calls.
    Calls submitBind(u32 set) {
        device->beginFrame();
        CommandList& cmd = device->acquireCommandList();
        cmd.beginRenderPass({.colorAttachments = {{.texture = device->swapchain()->acquireNextImage()}}});
        cmd.bindGroup(set, group);
        cmd.endRenderPass();
        gl().calls.clear();
        device->submit(cmd);
        device->endFrame();
        Calls binds;
        for (const std::string& call : gl().calls) {
            if (call.starts_with("Bind") && !call.starts_with("BindFramebuffer")) {
                binds.push_back(call);
            }
        }
        return binds;
    }

    test::FakeGlWindow window;
    std::unique_ptr<Device> device;
    TextureHandle texture;
    GLuint textureName = 0;
    SamplerHandle sampler;
    GLuint samplerName = 0;
    BufferHandle uniforms;
    GLuint uniformsName = 0;
    BindGroupLayoutHandle layout;
    BindGroupHandle group;
};

} // namespace

TEST_CASE("OpenGL backend: samplers set every parameter of their descriptor", "[rhi_opengl][sampler]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;

    SECTION("the default descriptor filters linearly within and between mips and clamps") {
        const CreatedSampler sampler = createSampler(*device, gl, {.debugName = "Default"});

        Calls expected = creation(sampler.name, {{"GL_TEXTURE_MIN_FILTER", "GL_LINEAR_MIPMAP_LINEAR"},
                                                 {"GL_TEXTURE_MAG_FILTER", "GL_LINEAR"},
                                                 {"GL_TEXTURE_WRAP_S", "GL_CLAMP_TO_EDGE"},
                                                 {"GL_TEXTURE_WRAP_T", "GL_CLAMP_TO_EDGE"},
                                                 {"GL_TEXTURE_WRAP_R", "GL_CLAMP_TO_EDGE"}});
        expected.push_back(std::format("ObjectLabel(GL_SAMPLER, {}, Default)", sampler.name));
        CHECK(sampler.calls == expected);
    }
    SECTION("nearest filters, each address mode, and a compare function") {
        const CreatedSampler sampler = createSampler(*device, gl,
                                                     {.minFilter = Filter::Nearest,
                                                      .magFilter = Filter::Nearest,
                                                      .mipmapMode = MipmapMode::Nearest,
                                                      .addressU = AddressMode::Repeat,
                                                      .addressV = AddressMode::MirroredRepeat,
                                                      .addressW = AddressMode::ClampToEdge,
                                                      .compare = CompareOp::LessOrEqual});

        const Calls expected =
            creation(sampler.name, {{"GL_TEXTURE_MIN_FILTER", "GL_NEAREST_MIPMAP_NEAREST"},
                                    {"GL_TEXTURE_MAG_FILTER", "GL_NEAREST"},
                                    {"GL_TEXTURE_WRAP_S", "GL_REPEAT"},
                                    {"GL_TEXTURE_WRAP_T", "GL_MIRRORED_REPEAT"},
                                    {"GL_TEXTURE_WRAP_R", "GL_CLAMP_TO_EDGE"},
                                    {"GL_TEXTURE_COMPARE_MODE", "GL_COMPARE_REF_TO_TEXTURE"},
                                    {"GL_TEXTURE_COMPARE_FUNC", "GL_LEQUAL"}});
        CHECK(sampler.calls == expected); // no name, no label
    }
    SECTION("the min filter holds the filtering between mips") {
        CHECK(createSampler(*device, gl, {.minFilter = Filter::Nearest, .mipmapMode = MipmapMode::Linear})
                  .calls[1]
                  .ends_with("GL_NEAREST_MIPMAP_LINEAR)"));
        CHECK(createSampler(*device, gl, {.minFilter = Filter::Linear, .mipmapMode = MipmapMode::Nearest})
                  .calls[1]
                  .ends_with("GL_LINEAR_MIPMAP_NEAREST)"));
    }
    SECTION("anisotropy is ignored without EXT_texture_filter_anisotropic") {
        const CreatedSampler sampler = createSampler(*device, gl, {.maxAnisotropy = 8.0F});
        for (const std::string& call : sampler.calls) {
            CHECK_FALSE(call.starts_with("SamplerParameterf"));
        }
    }
}

TEST_CASE("OpenGL backend: sampler anisotropy is clamped to the driver's limit", "[rhi_opengl][sampler]") {
    test::FakeGlWindow window;
    window.context.state.extensions.emplace_back("GL_EXT_texture_filter_anisotropic");
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;
    REQUIRE(device->limits().maxSamplerAnisotropy == 16.0F);

    const CreatedSampler eight = createSampler(*device, gl, {.maxAnisotropy = 8.0F});
    CHECK(eight.calls.back() ==
          std::format("SamplerParameterf({}, GL_TEXTURE_MAX_ANISOTROPY, 8)", eight.name));
    const CreatedSampler many = createSampler(*device, gl, {.maxAnisotropy = 64.0F});
    CHECK(many.calls.back() ==
          std::format("SamplerParameterf({}, GL_TEXTURE_MAX_ANISOTROPY, 16)", many.name));
    const CreatedSampler none = createSampler(*device, gl, {});
    CHECK_FALSE(none.calls.back().starts_with("SamplerParameterf"));
}

TEST_CASE("OpenGL backend: destroying a sampler deletes its GL object", "[rhi_opengl][sampler]") {
    test::FakeGlWindow window;
    test::FakeGlState& gl = window.context.state;
    {
        const auto device = requireOpenGl(window);
        const CreatedSampler destroyed = createSampler(*device, gl, {});
        const CreatedSampler kept = createSampler(*device, gl, {});
        REQUIRE(gl.liveSamplers.size() == 2);

        device->destroy(destroyed.handle);
        device->destroy(SamplerHandle{}); // null handles are ignored
        CHECK(gl.liveSamplers == std::set<GLuint>{kept.name});
    }
    CHECK(gl.liveSamplers.empty()); // the device deletes the rest
}

TEST_CASE("OpenGL backend: bind groups bind textures and samplers to the unit at flatBinding(set, binding)",
          "[rhi_opengl][sampler][binding]") {
    TextureScene scene;

    CHECK(
        scene.submitBind(kMaterialGroup) ==
        Calls{
            std::format("BindBufferRange(GL_UNIFORM_BUFFER, 16, {}, offset 0, size 64)", scene.uniformsName),
            std::format("BindTextureUnit(17, {})", scene.textureName), // set 2, binding 1
            std::format("BindSampler(17, {})", scene.samplerName),
        });
    CHECK(scene.submitBind(kFrameGroup) ==
          Calls{
              std::format("BindBufferRange(GL_UNIFORM_BUFFER, 0, {}, offset 0, size 64)", scene.uniformsName),
              std::format("BindTextureUnit(1, {})", scene.textureName),
              std::format("BindSampler(1, {})", scene.samplerName),
          });
}

#if TESERRA_ENABLE_ASSERTS

TEST_CASE("OpenGL backend: validation sees the device's samplers", "[rhi_opengl][sampler][validation]") {
    TextureScene scene;
    const auto failureOf = [](auto&& call) { return test::requireAssert(call).message(); };

    // A sampler that filters linearly cannot read a format without linear filtering.
    const TextureHandle heights = *scene.device->createTexture(
        {.format = Format::R32Float, .width = 4, .height = 4, .debugName = "Heights"});
    const std::array resources{
        BindGroupEntry{.binding = 1, .texture = heights, .sampler = scene.sampler},
        BindGroupEntry{.binding = 0, .buffer = {.buffer = scene.uniforms, .size = 64}},
    };
    CHECK(failureOf([&] {
              static_cast<void>(scene.device->createBindGroup(
                  {.layout = scene.layout, .entries = resources, .debugName = "G"}));
          }) ==
          "Bind group 'G': sampler 'Linear' at binding 1 filters linearly, but format R32Float of texture "
          "'Heights' is not filterable");

    // A sampler destroyed after the group was created is caught at submission.
    scene.device->destroy(scene.sampler);
    CHECK(
        failureOf([&] { static_cast<void>(scene.submitBind(kMaterialGroup)); }) ==
        std::format("bindGroup 'Material' uses sampler {} at binding 1, which is not alive", scene.sampler));
    scene.device->endFrame();
}

TEST_CASE("OpenGL backend: without validation, replay still catches stale textures and samplers",
          "[rhi_opengl][sampler][binding]") {
    TextureScene scene(/*validation=*/false);

    SECTION("texture") {
        scene.device->destroy(scene.texture);
        CHECK(test::requireAssert([&] { static_cast<void>(scene.submitBind(kMaterialGroup)); }).message() ==
              std::format("bindGroup 'Material' uses texture {} at binding 1, which is not alive",
                          scene.texture));
    }
    SECTION("sampler") {
        scene.device->destroy(scene.sampler);
        CHECK(test::requireAssert([&] { static_cast<void>(scene.submitBind(kMaterialGroup)); }).message() ==
              std::format("bindGroup 'Material' uses sampler {} at binding 1, which is not alive",
                          scene.sampler));
    }
    scene.device->endFrame();
}

#endif
