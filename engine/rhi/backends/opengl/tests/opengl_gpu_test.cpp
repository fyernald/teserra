// The OpenGL device on a real driver, in a hidden window. Skipped on machines without a window system or an
// OpenGL 4.5 driver, such as the CI runners; opengl_device_test.cpp covers the same paths with a fake driver.

#include <teserra/platform/opengl_context.hpp>
#include <teserra/platform/platform.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/shader.hpp>
#include <teserra/rhi/shader_blob.hpp>
#include <teserra/rhi_opengl/opengl_backend.hpp>
#include <teserra/test/log_capture.hpp>

#include "gl_device.hpp"
#include <catch2/catch_test_macros.hpp>
#include <glad/gl.h>
#include <rhi_opengl_tests/shaders.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <vector>

using namespace teserra;
using namespace teserra::rhi;

namespace {

struct GpuWindow {
    std::unique_ptr<platform::Platform> platform;
    std::unique_ptr<platform::Window> window;
};

GpuWindow createGpuWindowOrSkip() {
    Result<std::unique_ptr<platform::Platform>> platform = platform::Platform::create();
    if (!platform) {
        SKIP("No window system: " << std::format("{}", platform.error()));
    }
    Result<std::unique_ptr<platform::Window>> window = (*platform)->createWindow({
        .title = "Teserra OpenGL test",
        .size = {320, 200},
        .visible = false,
        .graphicsApi = platform::GraphicsApi::OpenGL,
        .debugContext = true,
    });
    if (!window) {
        SKIP("No OpenGL 4.5 driver: " << std::format("{}", window.error()));
    }
    return {std::move(platform).value(), std::move(window).value()};
}

GLADapiproc loadFunction(void* context, const char* name) {
    return static_cast<platform::OpenGlContext*>(context)->getProcAddress(name);
}

// A .tshader blob around GLSL that does not compile. teserra_shaderc refuses such a shader at build time, so
// the test writes the blob itself to reach the driver's compiler; the SPIR-V only has to be whole words.
std::vector<std::byte> brokenBlob(std::string_view glsl) {
    constexpr std::array<u32, 2> kSpirvWords{0x07230203, 0x00010500};
    return writeShaderBlob(
        {.stage = ShaderStage::Fragment, .spirv = std::as_bytes(std::span(kSpirvWords)), .glsl = glsl});
}

} // namespace

TEST_CASE("OpenGL backend (GPU): creates a device on the window's context", "[rhi_opengl][gpu]") {
    const GpuWindow gpu = createGpuWindowOrSkip();
    test::LogCapture log;

    auto created = createDevice({.backend = Backend::OpenGL, .enableBackendDebug = true}, gpu.window.get(),
                                std::array{opengl::backendFactory()});
    REQUIRE(created.has_value());
    Device& device = **created;
    CHECK(device.backend() == Backend::OpenGL);

    const DeviceLimits minimums{};
    CHECK(device.limits().maxTextureDimension2D >= minimums.maxTextureDimension2D);
    CHECK(device.limits().maxTextureArrayLayers >= minimums.maxTextureArrayLayers);
    CHECK(device.limits().uniformBufferOffsetAlignment <= minimums.uniformBufferOffsetAlignment);
    CHECK(device.limits().maxUniformBufferRange >= minimums.maxUniformBufferRange);
    CHECK(device.limits().maxSamplerAnisotropy >= 1.0F);

    // The test's own function table, to look at the state the device set up.
    platform::OpenGlContext& context = *gpu.window->openGlContext();
    GladGLContext gl{};
    REQUIRE(gladLoadGLContextUserPtr(&gl, &loadFunction, &context) != 0);

    GLint clipOrigin = 0;
    GLint clipDepthMode = 0;
    gl.GetIntegerv(GL_CLIP_ORIGIN, &clipOrigin);
    gl.GetIntegerv(GL_CLIP_DEPTH_MODE, &clipDepthMode);
    CHECK(clipOrigin == GL_UPPER_LEFT);
    CHECK(clipDepthMode == GL_ZERO_TO_ONE);

    log.clear();
    gl.DebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_ERROR, 42, GL_DEBUG_SEVERITY_HIGH, -1,
                          "Inserted by the test");
    bool logged = false;
    for (const test::CapturedLog& record : log.records()) {
        logged = logged || (record.level == LogLevel::Error && record.category == LogCategory::Rhi &&
                            record.message == "OpenGL Error 42 from Application: Inserted by the test");
    }
    CHECK(logged);

    device.beginFrame();
    device.submit(device.acquireCommandList());
    device.endFrame();
    device.waitIdle();
}

TEST_CASE("OpenGL backend (GPU): clears and presents frames", "[rhi_opengl][gpu]") {
    const GpuWindow gpu = createGpuWindowOrSkip();
    const test::LogCapture log;
    auto created =
        createDevice({.backend = Backend::OpenGL, .enableValidation = true, .enableBackendDebug = true},
                     gpu.window.get(), std::array{opengl::backendFactory()});
    REQUIRE(created.has_value());
    auto& device = static_cast<opengl::GlDevice&>(**created);
    Swapchain& swapchain = *device.swapchain();
    REQUIRE(swapchain.format() == Format::Rgba8Srgb);

    for (int frame = 0; frame < 10; ++frame) {
        device.beginFrame();
        const TextureHandle image = swapchain.acquireNextImage();
        REQUIRE_FALSE(image.isNull());
        CommandList& cmd = device.acquireCommandList();
        cmd.pushDebugGroup("Clear");
        cmd.beginRenderPass({.colorAttachments = {{
                                 .texture = image,
                                 .loadOp = LoadOp::Clear,
                                 .clearColor = {0.5F, 0.25F, 1.0F, 1.0F}, // linear
                             }}});
        cmd.endRenderPass();
        cmd.popDebugGroup();
        device.submit(cmd);

        if (frame == 0) {
            // The image holds the sRGB encoding of the linear clear color: 0.5 -> 188, 0.25 -> 137.
            std::array<GLubyte, 4> texel{};
            device.gl().GetTextureSubImage(device.texture(image)->name, 0, 0, 0, 0, 1, 1, 1, GL_RGBA,
                                           GL_UNSIGNED_BYTE, static_cast<GLsizei>(texel.size()),
                                           texel.data());
            CHECK(std::abs(texel[0] - 188) <= 1);
            CHECK(std::abs(texel[1] - 137) <= 1);
            CHECK(texel[2] == 255);
            CHECK(texel[3] == 255);
        }
        swapchain.present();
        device.endFrame();
    }

    // Resizing and minimizing: a new image, then none, then one again.
    swapchain.resize({64, 48});
    CHECK(swapchain.size() == UVec2{64, 48});
    swapchain.resize({0, 0});
    CHECK(swapchain.acquireNextImage().isNull());
    swapchain.resize({320, 200});
    CHECK_FALSE(swapchain.acquireNextImage().isNull());
    device.waitIdle();

    for (const test::CapturedLog& record : log.records()) {
        INFO(record.message);
        CHECK(record.level < LogLevel::Warn);
    }
}

TEST_CASE("OpenGL backend (GPU): texture uploads land top row first", "[rhi_opengl][gpu]") {
    const GpuWindow gpu = createGpuWindowOrSkip();
    const test::LogCapture log;
    auto created =
        createDevice({.backend = Backend::OpenGL, .enableValidation = true, .enableBackendDebug = true},
                     gpu.window.get(), std::array{opengl::backendFactory()});
    REQUIRE(created.has_value());
    auto& device = static_cast<opengl::GlDevice&>(**created);
    const auto readTexels = [&](TextureHandle texture, u32 layer, UVec2 size) {
        std::vector<GLubyte> texels(usize{size.x} * size.y * 4);
        device.gl().GetTextureSubImage(device.texture(texture)->name, 0, 0, 0, static_cast<GLint>(layer),
                                       static_cast<GLsizei>(size.x), static_cast<GLsizei>(size.y), 1, GL_RGBA,
                                       GL_UNSIGNED_BYTE, static_cast<GLsizei>(texels.size()), texels.data());
        return texels;
    };

    SECTION("a 2D texture with padded rows; the top row is row 0") {
        const TextureHandle texture = *device.createTexture(
            {.format = Format::Rgba8Unorm, .width = 2, .height = 3, .debugName = "Rows"});
        // Each row's texels hold their row number; rows are 12 bytes apart, the last 4 are padding (0xEE).
        std::vector<std::byte> data;
        for (u8 row = 0; row < 3; ++row) {
            for (int value = 0; value < 8; ++value) {
                data.push_back(std::byte{row});
            }
            if (row < 2) {
                data.insert(data.end(), 4, std::byte{0xEE});
            }
        }
        device.writeTexture(texture, {.width = 2, .height = 3}, data, 12);

        const std::vector<GLubyte> texels = readTexels(texture, 0, {2, 3});
        for (usize index = 0; index < texels.size(); ++index) {
            INFO("byte " << index);
            CHECK(texels[index] == index / 8); // 8 bytes per row
        }
        device.destroy(texture);
    }
    SECTION("a layer of an array texture, BGRA texels") {
        const TextureHandle texture = *device.createTexture({.dimension = TextureDimension::Tex2DArray,
                                                             .format = Format::Bgra8Unorm,
                                                             .width = 1,
                                                             .height = 1,
                                                             .depthOrLayers = 2,
                                                             .debugName = "Layers"});
        const std::array data{std::byte{10}, std::byte{20}, std::byte{30}, std::byte{40}}; // B, G, R, A
        device.writeTexture(texture, {.arrayLayer = 1, .width = 1, .height = 1}, data);

        CHECK(readTexels(texture, 1, {1, 1}) == std::vector<GLubyte>{30, 20, 10, 40}); // read as RGBA
        device.destroy(texture);
    }

    device.waitIdle();
    for (const test::CapturedLog& record : log.records()) {
        INFO(record.message);
        CHECK(record.level < LogLevel::Warn);
    }
}

TEST_CASE("OpenGL backend (GPU): sRGB textures filter in linear space", "[rhi_opengl][gpu]") {
    const GpuWindow gpu = createGpuWindowOrSkip();
    const test::LogCapture log;
    auto created =
        createDevice({.backend = Backend::OpenGL, .enableValidation = true, .enableBackendDebug = true},
                     gpu.window.get(), std::array{opengl::backendFactory()});
    REQUIRE(created.has_value());
    auto& device = static_cast<opengl::GlDevice&>(**created);

    // Black and white texels average to linear 0.5. Mip generation filters like sampling does, so the 1x1
    // level is the reference: the sRGB encoding of 0.5 (188) for an sRGB texture, and 0.5 itself (128) for a
    // linear one, which would also be what a wrongly decoded sRGB texture gives.
    constexpr std::array<u8, 16> kCheckers{0,   0,   0,   255, 255, 255, 255, 255,
                                           255, 255, 255, 255, 0,   0,   0,   255};
    const auto averageOf = [&](Format format) {
        const TextureHandle texture = *device.createTexture(
            {.format = format, .width = 2, .height = 2, .mipLevels = kAllMips, .debugName = "Checkers"});
        device.writeTexture(texture, {.width = 2, .height = 2}, std::as_bytes(std::span(kCheckers)));
        device.generateMipmaps(texture);
        std::array<GLubyte, 4> texel{};
        device.gl().GetTextureSubImage(device.texture(texture)->name, 1, 0, 0, 0, 1, 1, 1, GL_RGBA,
                                       GL_UNSIGNED_BYTE, static_cast<GLsizei>(texel.size()), texel.data());
        device.destroy(texture);
        return texel;
    };

    const std::array<GLubyte, 4> srgb = averageOf(Format::Rgba8Srgb);
    const std::array<GLubyte, 4> linear = averageOf(Format::Rgba8Unorm);
    for (usize channel = 0; channel < 3; ++channel) {
        INFO("channel " << channel);
        CHECK(std::abs(srgb[channel] - 188) <= 2);
        CHECK(std::abs(linear[channel] - 128) <= 1);
    }
    CHECK(srgb[3] == 255);
    CHECK(linear[3] == 255);

    device.waitIdle();
    for (const test::CapturedLog& record : log.records()) {
        INFO(record.message);
        CHECK(record.level < LogLevel::Warn);
    }
}

TEST_CASE("OpenGL backend (GPU): depth attachments are cleared and stored", "[rhi_opengl][gpu]") {
    const GpuWindow gpu = createGpuWindowOrSkip();
    const test::LogCapture log;
    auto created =
        createDevice({.backend = Backend::OpenGL, .enableValidation = true, .enableBackendDebug = true},
                     gpu.window.get(), std::array{opengl::backendFactory()});
    REQUIRE(created.has_value());
    auto& device = static_cast<opengl::GlDevice&>(**created);
    const TextureHandle depth = *device.createTexture({.format = Format::Depth32Float,
                                                       .width = 4,
                                                       .height = 4,
                                                       .usage = TextureUsage::DepthStencil,
                                                       .debugName = "Depth"});

    device.beginFrame();
    CommandList& cmd = device.acquireCommandList();
    cmd.beginRenderPass(
        {.depthStencil = {.texture = depth, .depthStoreOp = StoreOp::Store, .clearDepth = 0.25F}});
    cmd.endRenderPass();
    device.submit(cmd);
    device.endFrame();
    device.waitIdle();

    std::array<GLfloat, 16> values{};
    device.gl().GetTextureSubImage(device.texture(depth)->name, 0, 0, 0, 0, 4, 4, 1, GL_DEPTH_COMPONENT,
                                   GL_FLOAT, static_cast<GLsizei>(sizeof(values)), values.data());
    for (const GLfloat value : values) {
        CHECK(value == 0.25F);
    }
    device.destroy(depth);

    for (const test::CapturedLog& record : log.records()) {
        INFO(record.message);
        CHECK(record.level < LogLevel::Warn);
    }
}

TEST_CASE("OpenGL backend (GPU): buffer uploads round trip through readback", "[rhi_opengl][gpu]") {
    const GpuWindow gpu = createGpuWindowOrSkip();
    const test::LogCapture log;
    auto created =
        createDevice({.backend = Backend::OpenGL, .enableValidation = true, .enableBackendDebug = true},
                     gpu.window.get(), std::array{opengl::backendFactory()});
    REQUIRE(created.has_value());
    Device& device = **created;

    constexpr usize kSize = 1024;
    std::vector<std::byte> pattern(kSize);
    for (usize i = 0; i < kSize; ++i) {
        pattern[i] = static_cast<std::byte>((i * 7) & 0xFFU);
    }
    const BufferHandle gpuOnly = *device.createBuffer({.sizeInBytes = kSize,
                                                       .usage = BufferUsage::CopySrc | BufferUsage::CopyDst,
                                                       .debugName = "Round trip: GpuOnly"});
    const BufferHandle upload = *device.createBuffer({.sizeInBytes = kSize,
                                                      .usage = BufferUsage::CopySrc,
                                                      .memory = MemoryLocation::Upload,
                                                      .debugName = "Round trip: Upload"});
    const BufferHandle readback = *device.createBuffer({.sizeInBytes = 2 * kSize,
                                                        .usage = BufferUsage::CopyDst,
                                                        .memory = MemoryLocation::Readback,
                                                        .debugName = "Round trip: Readback"});

    device.writeBuffer(gpuOnly, 0, pattern);
    std::ranges::reverse_copy(pattern, device.map(upload).begin());

    device.beginFrame();
    CommandList& cmd = device.acquireCommandList();
    cmd.copyBuffer({.source = gpuOnly, .destination = readback, .size = kSize});
    cmd.copyBuffer({.source = upload, .destination = readback, .destinationOffset = kSize, .size = kSize});
    device.submit(cmd);
    device.endFrame();
    device.waitIdle();

    const std::span<const std::byte> result = device.map(readback);
    REQUIRE(result.size() == 2 * kSize);
    CHECK(std::ranges::equal(result.first(kSize), pattern));
    CHECK(std::ranges::equal(result.last(kSize), std::views::reverse(pattern)));

    device.destroy(gpuOnly);
    device.destroy(upload);
    device.destroy(readback);
    for (const test::CapturedLog& record : log.records()) {
        INFO(record.message);
        CHECK(record.level < LogLevel::Warn);
    }
}

TEST_CASE("OpenGL backend (GPU): compiles .tshader blobs and reports driver errors with line numbers",
          "[rhi_opengl][gpu]") {
    const GpuWindow gpu = createGpuWindowOrSkip();
    auto created =
        createDevice({.backend = Backend::OpenGL, .enableValidation = true, .enableBackendDebug = true},
                     gpu.window.get(), std::array{opengl::backendFactory()});
    REQUIRE(created.has_value());
    Device& device = **created;
    const test::LogCapture log;

    const std::span<const std::byte> vertexBlob = test::shaders::kVertexColorVert;
    const std::span<const std::byte> fragmentBlob = test::shaders::kVertexColorFrag;
    const auto vertex = device.createShaderModule(
        {.stage = ShaderStage::Vertex, .blob = vertexBlob, .debugName = "GPU test vertex"});
    const auto fragment = device.createShaderModule(
        {.stage = ShaderStage::Fragment, .blob = fragmentBlob, .debugName = "GPU test fragment"});
    REQUIRE(vertex.has_value());
    REQUIRE(fragment.has_value());
    for (const test::CapturedLog& record : log.records()) {
        INFO(record.message);
        CHECK(record.level < LogLevel::Warn);
    }

    // An undeclared identifier on line 4.
    const std::vector<std::byte> broken = brokenBlob(R"(#version 450 core
layout(location = 0) out vec4 fragColor;
void main() {
    fragColor = undeclaredColor;
}
)");
    const auto brokenShader = device.createShaderModule(
        {.stage = ShaderStage::Fragment, .blob = broken, .debugName = "GPU test broken"});
    REQUIRE_FALSE(brokenShader.has_value());
    CHECK(brokenShader.error().code == ErrorCode::CompilationFailed);
    const std::string& message = brokenShader.error().message;
    INFO(message);
    CHECK(message.starts_with("Shader module 'GPU test broken' (fragment) failed to compile:\n"));
    // Drivers write the line as "0(4)" (NVIDIA) or "0:4:" (Intel, AMD, Mesa).
    CHECK((message.find("(4)") != std::string::npos || message.find(":4:") != std::string::npos));

    device.destroy(*vertex);
    device.destroy(*fragment);
}

TEST_CASE("OpenGL backend (GPU): creates and binds a graphics pipeline", "[rhi_opengl][gpu]") {
    const GpuWindow gpu = createGpuWindowOrSkip();
    auto created =
        createDevice({.backend = Backend::OpenGL, .enableValidation = true, .enableBackendDebug = true},
                     gpu.window.get(), std::array{opengl::backendFactory()});
    REQUIRE(created.has_value());
    Device& device = **created;
    const test::LogCapture log;

    const auto vertex = device.createShaderModule({.stage = ShaderStage::Vertex,
                                                   .blob = test::shaders::kVertexColorVert,
                                                   .debugName = "GPU pipeline vertex"});
    const auto fragment = device.createShaderModule({.stage = ShaderStage::Fragment,
                                                     .blob = test::shaders::kVertexColorFrag,
                                                     .debugName = "GPU pipeline fragment"});
    REQUIRE(vertex.has_value());
    REQUIRE(fragment.has_value());

    constexpr std::array kBuffers{VertexBufferLayout{.stride = 28}};
    constexpr std::array kAttributes{
        VertexAttribute{.location = 0, .format = VertexFormat::Float3},
        VertexAttribute{.location = 1, .format = VertexFormat::Float4, .offset = 12},
    };
    const auto pipeline = device.createGraphicsPipeline({
        .vertexShader = *vertex,
        .fragmentShader = *fragment,
        .vertexBuffers = kBuffers,
        .vertexAttributes = kAttributes,
        .raster = {.cullMode = CullMode::Back},
        .colorBlend = {{.enabled = true,
                        .srcColor = BlendFactor::SrcAlpha,
                        .dstColor = BlendFactor::OneMinusSrcAlpha}},
        .targets = {.colorFormats = {device.swapchain()->format()}},
        .debugName = "GPU pipeline",
    });
    REQUIRE(pipeline.has_value());

    device.beginFrame();
    CommandList& cmd = device.acquireCommandList();
    cmd.beginRenderPass({.colorAttachments = {{.texture = device.swapchain()->acquireNextImage()}}});
    cmd.bindPipeline(*pipeline);
    cmd.bindPipeline(*pipeline);
    cmd.endRenderPass();
    device.submit(cmd);
    device.endFrame();
    device.waitIdle();

    device.destroy(*pipeline);
    device.destroy(*vertex);
    device.destroy(*fragment);
    for (const test::CapturedLog& record : log.records()) {
        INFO(record.message);
        CHECK(record.level < LogLevel::Warn);
    }
}

TEST_CASE("OpenGL backend (GPU): front faces are counter-clockwise and row 0 is the top",
          "[rhi_opengl][gpu]") {
    const GpuWindow gpu = createGpuWindowOrSkip();
    auto created =
        createDevice({.backend = Backend::OpenGL, .enableValidation = true, .enableBackendDebug = true},
                     gpu.window.get(), std::array{opengl::backendFactory()});
    REQUIRE(created.has_value());
    auto& device = static_cast<opengl::GlDevice&>(**created);
    const test::LogCapture log;

    const auto vertex = device.createShaderModule({.stage = ShaderStage::Vertex,
                                                   .blob = test::shaders::kVertexColorVert,
                                                   .debugName = "Winding vertex"});
    const auto fragment = device.createShaderModule({.stage = ShaderStage::Fragment,
                                                     .blob = test::shaders::kVertexColorFrag,
                                                     .debugName = "Winding fragment"});
    REQUIRE(vertex.has_value());
    REQUIRE(fragment.has_value());
    constexpr std::array kBuffers{VertexBufferLayout{.stride = 28}};
    constexpr std::array kAttributes{
        VertexAttribute{.location = 0, .format = VertexFormat::Float3},
        VertexAttribute{.location = 1, .format = VertexFormat::Float4, .offset = 12},
    };
    const auto pipeline = device.createGraphicsPipeline({
        .vertexShader = *vertex,
        .fragmentShader = *fragment,
        .vertexBuffers = kBuffers,
        .vertexAttributes = kAttributes,
        .raster = {.cullMode = CullMode::Back, .frontFace = FrontFace::CounterClockwise},
        .targets = {.colorFormats = {device.swapchain()->format()}},
        .debugName = "Winding",
    });
    REQUIRE(pipeline.has_value());

    // NDC, +Y up: a green counter-clockwise triangle in the top-left corner and a red clockwise one in the
    // top-right corner. With back faces culled, only the green one may appear, and at the top.
    constexpr std::array<f32, usize{6} * 7> kVertices{
        -1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F, // counter-clockwise
        -1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F, //
        0.0F,  1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F, //
        1.0F,  1.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, // clockwise
        1.0F,  0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, //
        0.0F,  1.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, //
    };
    const BufferHandle vertices = *device.createBuffer(
        {.sizeInBytes = sizeof(kVertices), .usage = BufferUsage::Vertex | BufferUsage::CopyDst});
    device.writeBuffer(vertices, 0, std::as_bytes(std::span(kVertices)));

    device.beginFrame();
    const TextureHandle image = device.swapchain()->acquireNextImage();
    CommandList& cmd = device.acquireCommandList();
    cmd.beginRenderPass(
        {.colorAttachments = {
             {.texture = image, .loadOp = LoadOp::Clear, .clearColor = {0.0F, 0.0F, 0.0F, 1.0F}}}});
    cmd.bindPipeline(*pipeline);
    cmd.bindVertexBuffer(0, vertices);
    cmd.draw({.vertexCount = 6});
    cmd.endRenderPass();
    device.submit(cmd);

    const auto texel = [&](u32 x, u32 y) {
        std::array<GLubyte, 4> rgba{};
        device.gl().GetTextureSubImage(device.texture(image)->name, 0, static_cast<GLint>(x),
                                       static_cast<GLint>(y), 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                                       static_cast<GLsizei>(rgba.size()), rgba.data());
        return rgba;
    };
    const UVec2 size = device.swapchain()->size();
    constexpr std::array<GLubyte, 4> kGreen{0, 255, 0, 255};
    constexpr std::array<GLubyte, 4> kBlack{0, 0, 0, 255};
    CHECK(texel(10, 10) == kGreen);          // the counter-clockwise triangle, at the top
    CHECK(texel(size.x - 10, 10) == kBlack); // the clockwise one is culled
    CHECK(texel(10, size.y - 10) == kBlack); // nothing at the bottom
    device.swapchain()->present();
    device.endFrame();
    device.waitIdle();

    device.destroy(vertices);
    device.destroy(*pipeline);
    device.destroy(*vertex);
    device.destroy(*fragment);
    for (const test::CapturedLog& record : log.records()) {
        INFO(record.message);
        CHECK(record.level < LogLevel::Warn);
    }
}

TEST_CASE("OpenGL backend (GPU): bind groups at sets 0, 2 and 3 reach the shader", "[rhi_opengl][gpu]") {
    const GpuWindow gpu = createGpuWindowOrSkip();
    auto created =
        createDevice({.backend = Backend::OpenGL, .enableValidation = true, .enableBackendDebug = true},
                     gpu.window.get(), std::array{opengl::backendFactory()});
    REQUIRE(created.has_value());
    auto& device = static_cast<opengl::GlDevice&>(**created);
    const test::LogCapture log;

    // The shader source declares (set, binding); the blob's GLSL and the backend agree on the GL index.
    const auto vertex = device.createShaderModule(
        {.stage = ShaderStage::Vertex, .blob = test::shaders::kBindingSetsVert, .debugName = "Sets vertex"});
    const auto fragment = device.createShaderModule({.stage = ShaderStage::Fragment,
                                                     .blob = test::shaders::kBindingSetsFrag,
                                                     .debugName = "Sets fragment"});
    REQUIRE(vertex.has_value());
    REQUIRE(fragment.has_value());
    constexpr std::array kFrameEntries{
        BindGroupLayoutEntry{.binding = 0, .visibility = ShaderStage::Fragment}};
    constexpr std::array kMaterialEntries{
        BindGroupLayoutEntry{.binding = 0, .visibility = ShaderStage::Fragment},
        BindGroupLayoutEntry{.binding = 1, .visibility = ShaderStage::Fragment},
    };
    constexpr std::array kDrawEntries{
        BindGroupLayoutEntry{.binding = 0, .visibility = ShaderStage::Fragment, .hasDynamicOffset = true}};
    const BindGroupLayoutHandle frameLayout = *device.createBindGroupLayout({.entries = kFrameEntries});
    const BindGroupLayoutHandle materialLayout = *device.createBindGroupLayout({.entries = kMaterialEntries});
    const BindGroupLayoutHandle drawLayout = *device.createBindGroupLayout({.entries = kDrawEntries});
    const std::array groups{frameLayout, BindGroupLayoutHandle{}, materialLayout, drawLayout};
    const PipelineLayoutHandle layout = *device.createPipelineLayout({.groups = groups});
    const auto pipeline = device.createGraphicsPipeline({
        .vertexShader = *vertex,
        .fragmentShader = *fragment,
        .layout = layout,
        .targets = {.colorFormats = {Format::Rgba8Unorm}},
        .debugName = "Sets",
    });
    REQUIRE(pipeline.has_value());

    // The spike's check (#36): the linked program's blocks sit at flatBinding(set, binding).
    const GladGLContext& gl = device.gl();
    const GLuint program = device.pipeline(*pipeline)->program;
    const auto blockBinding = [&](const char* name) {
        const GLuint index = gl.GetProgramResourceIndex(program, GL_UNIFORM_BLOCK, name);
        const GLenum property = GL_BUFFER_BINDING;
        GLint binding = -1;
        gl.GetProgramResourceiv(program, GL_UNIFORM_BLOCK, index, 1, &property, 1, nullptr, &binding);
        return binding;
    };
    CHECK(blockBinding("Frame") == static_cast<GLint>(flatBinding(0, 0)));
    CHECK(blockBinding("Material") == 16);
    CHECK(blockBinding("MaterialExtra") == 17);
    CHECK(blockBinding("Draw") == 24);

    // One vec4 per slot, each slot at the device's uniform buffer offset alignment: red, green, blue, and two
    // alphas that the draws pick with their dynamic offset.
    const u32 slot = device.limits().uniformBufferOffsetAlignment;
    const std::array<std::array<f32, 4>, 5> values{{
        {0.2F, 0.0F, 0.0F, 0.0F},
        {0.0F, 0.4F, 0.0F, 0.0F},
        {0.0F, 0.0F, 0.6F, 0.0F},
        {0.0F, 0.0F, 0.0F, 0.25F},
        {0.0F, 0.0F, 0.0F, 0.75F},
    }};
    const BufferHandle uniforms = *device.createBuffer({.sizeInBytes = u64{slot} * values.size(),
                                                        .usage = BufferUsage::Uniform | BufferUsage::CopyDst,
                                                        .debugName = "Sets uniforms"});
    for (usize i = 0; i < values.size(); ++i) {
        device.writeBuffer(uniforms, u64{slot} * i, std::as_bytes(std::span(values[i])));
    }
    const auto range = [&](u32 index) {
        return BufferBinding{.buffer = uniforms, .offset = u64{slot} * index, .size = 16};
    };
    const std::array frameResources{BindGroupEntry{.binding = 0, .buffer = range(0)}};
    const std::array materialResources{BindGroupEntry{.binding = 0, .buffer = range(1)},
                                       BindGroupEntry{.binding = 1, .buffer = range(2)}};
    const std::array drawResources{BindGroupEntry{.binding = 0, .buffer = range(3)}};
    const BindGroupHandle frameGroup =
        *device.createBindGroup({.layout = frameLayout, .entries = frameResources});
    const BindGroupHandle materialGroup =
        *device.createBindGroup({.layout = materialLayout, .entries = materialResources});
    const BindGroupHandle drawGroup =
        *device.createBindGroup({.layout = drawLayout, .entries = drawResources});

    // The left half with the first alpha, the right half with the second.
    constexpr UVec2 kSize{64, 32};
    const TextureHandle target =
        device.createTexture2D(Format::Rgba8Unorm, kSize, TextureUsage::RenderTarget, "Sets target");
    device.beginFrame();
    CommandList& cmd = device.acquireCommandList();
    cmd.beginRenderPass({.colorAttachments = {{.texture = target, .loadOp = LoadOp::Clear}}});
    cmd.bindPipeline(*pipeline);
    cmd.bindGroup(kFrameGroup, frameGroup);
    cmd.bindGroup(kMaterialGroup, materialGroup);
    const std::array firstAlpha{u32{0}};
    const std::array secondAlpha{slot};
    cmd.setScissor({.width = kSize.x / 2, .height = kSize.y});
    cmd.bindGroup(kDrawGroup, drawGroup, firstAlpha);
    cmd.draw({.vertexCount = 3});
    cmd.setScissor({.x = kSize.x / 2, .width = kSize.x / 2, .height = kSize.y});
    cmd.bindGroup(kDrawGroup, drawGroup, secondAlpha);
    cmd.draw({.vertexCount = 3});
    cmd.endRenderPass();
    device.submit(cmd);
    device.endFrame();

    const auto texel = [&](u32 x, u32 y) {
        std::array<GLubyte, 4> rgba{};
        gl.GetTextureSubImage(device.texture(target)->name, 0, static_cast<GLint>(x), static_cast<GLint>(y),
                              0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(rgba.size()),
                              rgba.data());
        return rgba;
    };
    const auto near = [](const std::array<GLubyte, 4>& actual, const std::array<int, 4>& expected) {
        for (usize i = 0; i < actual.size(); ++i) {
            if (std::abs(actual[i] - expected[i]) > 1) {
                return false;
            }
        }
        return true;
    };
    const std::array<GLubyte, 4> left = texel(8, 16);
    const std::array<GLubyte, 4> right = texel(kSize.x - 8, 16);
    INFO(std::format("left {} {} {} {}, right {} {} {} {}", left[0], left[1], left[2], left[3], right[0],
                     right[1], right[2], right[3]));
    CHECK(near(left, {51, 102, 153, 64}));   // 0.2, 0.4, 0.6, 0.25
    CHECK(near(right, {51, 102, 153, 191})); // 0.2, 0.4, 0.6, 0.75
    device.waitIdle();

    device.releaseTexture(target);
    device.destroy(drawGroup);
    device.destroy(materialGroup);
    device.destroy(frameGroup);
    device.destroy(uniforms);
    device.destroy(*pipeline);
    device.destroy(layout);
    device.destroy(drawLayout);
    device.destroy(materialLayout);
    device.destroy(frameLayout);
    device.destroy(*vertex);
    device.destroy(*fragment);
    for (const test::CapturedLog& record : log.records()) {
        INFO(record.message);
        CHECK(record.level < LogLevel::Warn);
    }
}
