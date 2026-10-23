// The descriptor checks every backend runs at creation (<teserra/rhi/validation.hpp>).

#include <teserra/rhi/validation.hpp>

#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <utility>

using namespace teserra;
using namespace teserra::rhi;

namespace {

template <typename Function>
std::string failureOf(Function&& function) {
    return test::requireAssert(std::forward<Function>(function)).message();
}

constexpr std::array<std::byte, 1> kCode{std::byte{1}};
constexpr ShaderModuleHandle kShader{0, 1};

} // namespace

#if TESERRA_ENABLE_ASSERTS

TEST_CASE("validateBufferDesc", "[rhi][validation]") {
    validateBufferDesc({.sizeInBytes = 16, .usage = BufferUsage::Vertex});
    validateBufferDesc(
        {.sizeInBytes = 16, .usage = BufferUsage::CopyDst, .memory = MemoryLocation::Readback});

    CHECK(failureOf([] { validateBufferDesc({.usage = BufferUsage::Vertex, .debugName = "Empty"}); }) ==
          "Buffer 'Empty' has a size of 0");
    CHECK(failureOf([] { validateBufferDesc({.sizeInBytes = 4, .debugName = "Aimless"}); }) ==
          "Buffer 'Aimless' has no usage");
    CHECK(failureOf([] {
              validateBufferDesc({.sizeInBytes = 4,
                                  .usage = BufferUsage::Vertex,
                                  .memory = MemoryLocation::Readback,
                                  .debugName = "Readback"});
          }) == "Readback buffer 'Readback' needs BufferUsage::CopyDst, the only way the GPU writes into it");
}

TEST_CASE("validateTextureDesc", "[rhi][validation]") {
    const DeviceLimits limits{};
    validateTextureDesc({.width = 256, .height = 128, .mipLevels = 9}, limits);
    validateTextureDesc({.width = 256, .height = 128, .mipLevels = kAllMips}, limits);
    validateTextureDesc({.dimension = TextureDimension::Cube, .depthOrLayers = 6}, limits);
    validateTextureDesc({.format = Format::Depth32Float, .usage = TextureUsage::DepthStencil}, limits);

    CHECK(failureOf([&] { validateTextureDesc({.format = Format::Undefined, .debugName = "T"}, limits); }) ==
          "Texture 'T' has no format");
    CHECK(failureOf([&] { validateTextureDesc({.height = 0, .debugName = "T"}, limits); }) ==
          "Texture 'T' has an empty size 1x0x1");
    CHECK(failureOf([&] { validateTextureDesc({.width = 16384, .debugName = "T"}, limits); }) ==
          "Texture 'T' is 16384x1, larger than the device's 8192");
    CHECK(failureOf([&] {
              validateTextureDesc({.width = 256, .height = 128, .mipLevels = 10, .debugName = "T"}, limits);
          }) == "Texture 'T' has 10 mip levels; 256x128 has at most 9");
    CHECK(failureOf([&] { validateTextureDesc({.depthOrLayers = 2, .debugName = "T"}, limits); }) ==
          "2D texture 'T' has a depth of 2");
    CHECK(failureOf([&] {
              validateTextureDesc({.dimension = TextureDimension::Cube, .depthOrLayers = 4, .debugName = "T"},
                                  limits);
          }) == "Cube texture 'T' has 4 faces instead of 6");
    CHECK(failureOf([&] {
              validateTextureDesc({.format = Format::Depth32Float, .debugName = "T"}, limits);
          }) == "Texture 'T': TextureUsage::DepthStencil is for depth formats only, and depth formats need "
                "it (format "
                "Depth32Float)");
    CHECK(failureOf([&] {
              validateTextureDesc({.format = Format::Depth32Float,
                                   .usage = TextureUsage::DepthStencil | TextureUsage::RenderTarget,
                                   .debugName = "T"},
                                  limits);
          }) == "Depth texture 'T' cannot be a color render target or storage texture");
}

TEST_CASE("validateSamplerDesc and validateShaderModuleDesc", "[rhi][validation]") {
    validateSamplerDesc({.maxAnisotropy = 16.0F});
    validateShaderModuleDesc({.stage = ShaderStage::Fragment, .blob = kCode});

    CHECK(failureOf([] { validateSamplerDesc({.maxAnisotropy = 0.0F, .debugName = "S"}); }) ==
          "Sampler 'S' has a maximum anisotropy of 0, below 1");
    CHECK(failureOf([] { validateShaderModuleDesc({.debugName = "Empty"}); }) ==
          "Shader module 'Empty' has no code");
    CHECK(failureOf([] {
              validateShaderModuleDesc(
                  {.stage = ShaderStage::Vertex | ShaderStage::Fragment, .blob = kCode, .debugName = "Both"});
          }) == "Shader module 'Both' must have exactly one stage");
    CHECK(failureOf([] {
              validateShaderModuleDesc({.stage = ShaderStage::Compute, .blob = kCode, .debugName = "Cull"});
          }) == "Shader module 'Cull' is a compute shader; the RHI has no compute pipelines yet");
}

TEST_CASE("validateGraphicsPipelineDesc", "[rhi][validation]") {
    constexpr std::array kBuffers{VertexBufferLayout{.stride = 16}};
    constexpr std::array kAttributes{
        VertexAttribute{.location = 0, .format = VertexFormat::Float2},
        VertexAttribute{.location = 1, .format = VertexFormat::Float2, .offset = 8},
    };
    validateGraphicsPipelineDesc({
        .vertexShader = kShader,
        .vertexBuffers = kBuffers,
        .vertexAttributes = kAttributes,
        .targets = {.colorFormats = {Format::Rgba8Srgb, Format::Rgba16Float},
                    .depthStencilFormat = Format::Depth32Float},
    });
    validateGraphicsPipelineDesc(
        {.vertexShader = kShader, .targets = {.depthStencilFormat = Format::Depth16Unorm}});

    CHECK(failureOf([] {
              validateGraphicsPipelineDesc(
                  {.targets = {.colorFormats = {Format::Rgba8Srgb}}, .debugName = "P"});
          }) == "Graphics pipeline 'P' has no vertex shader");
    CHECK(failureOf([] { validateGraphicsPipelineDesc({.vertexShader = kShader, .debugName = "P"}); }) ==
          "Graphics pipeline 'P' has no render targets");
    CHECK(failureOf([] {
              constexpr std::array kOrphan{VertexAttribute{.location = 3, .bufferIndex = 1}};
              validateGraphicsPipelineDesc({.vertexShader = kShader,
                                            .vertexAttributes = kOrphan,
                                            .targets = {.colorFormats = {Format::Rgba8Srgb}},
                                            .debugName = "P"});
          }) == "Graphics pipeline 'P': attribute at location 3 reads vertex buffer 1, but 0 are declared");
    CHECK(failureOf([&] {
              constexpr std::array kTwice{VertexAttribute{.location = 2},
                                          VertexAttribute{.location = 2, .offset = 4}};
              validateGraphicsPipelineDesc({.vertexShader = kShader,
                                            .vertexBuffers = kBuffers,
                                            .vertexAttributes = kTwice,
                                            .targets = {.colorFormats = {Format::Rgba8Srgb}},
                                            .debugName = "P"});
          }) == "Graphics pipeline 'P' has two attributes at location 2");
    CHECK(failureOf([] {
              validateGraphicsPipelineDesc(
                  {.vertexShader = kShader,
                   .targets = {.colorFormats = {Format::Undefined, Format::Rgba8Srgb}},
                   .debugName = "P"});
          }) == "Graphics pipeline 'P': color target 1 follows an unused slot");
    CHECK(failureOf([] {
              validateGraphicsPipelineDesc({.vertexShader = kShader,
                                            .targets = {.colorFormats = {Format::Depth32Float}},
                                            .debugName = "P"});
          }) == "Graphics pipeline 'P': color target 0 has the depth format Depth32Float");
    CHECK(failureOf([] {
              validateGraphicsPipelineDesc({.vertexShader = kShader,
                                            .targets = {.depthStencilFormat = Format::Rgba8Unorm},
                                            .debugName = "P"});
          }) == "Graphics pipeline 'P': the depth-stencil target has the color format Rgba8Unorm");
}

#endif
