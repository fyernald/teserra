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

TEST_CASE("validateTextureDesc checks usages against the format support table", "[rhi][validation]") {
    const DeviceLimits limits{};
    validateTextureDesc({.format = Format::Rgba8Unorm, .usage = TextureUsage::Storage}, limits);
    validateTextureDesc({.format = Format::R32Float, .usage = TextureUsage::RenderTarget}, limits);
    validateTextureDesc(
        {.format = Format::Depth16Unorm, .usage = TextureUsage::DepthStencil | TextureUsage::Sampled},
        limits);

    CHECK(failureOf([&] {
              validateTextureDesc(
                  {.format = Format::Rgba8Srgb, .usage = TextureUsage::Storage, .debugName = "T"}, limits);
          }) == "Texture 'T': format Rgba8Srgb does not support TextureUsage::Storage");
    CHECK(failureOf([&] {
              validateTextureDesc({.format = Format::Rg16Float,
                                   .usage = TextureUsage::Sampled | TextureUsage::Storage,
                                   .debugName = "T"},
                                  limits);
          }) == "Texture 'T': format Rg16Float does not support TextureUsage::Storage");
}

TEST_CASE("validateWriteTexture", "[rhi][validation]") {
    const TextureDesc atlas{
        .format = Format::Rgba8Srgb, .width = 64, .height = 32, .mipLevels = 3, .debugName = "Atlas"};
    const TextureDesc tiles{.dimension = TextureDimension::Tex2DArray,
                            .format = Format::R8Unorm,
                            .width = 16,
                            .height = 16,
                            .depthOrLayers = 4,
                            .debugName = "Tiles"};
    validateWriteTexture(atlas, {.width = 64, .height = 32}, usize{64} * 32 * 4, 0);
    // Mip 2 is 16x8; rows padded to 128 bytes: 7 * 128 + 64.
    validateWriteTexture(atlas, {.mipLevel = 2, .width = 16, .height = 8}, (7 * 128) + 64, 128);
    validateWriteTexture(tiles, {.arrayLayer = 1, .layerCount = 3, .x = 8, .y = 8, .width = 8, .height = 8},
                         usize{3} * 8 * 8, 0);

    CHECK(
        failureOf([&] { validateWriteTexture(atlas, {.x = 60, .width = 8, .height = 1}, 1024, 0); }) ==
        "writeTexture region (60, 0) 8x1, layers 0+1 is outside texture 'Atlas' (64x32 at mip 0, 1 layers)");
    CHECK(
        failureOf([&] { validateWriteTexture(atlas, {.mipLevel = 1, .width = 64, .height = 1}, 1024, 0); }) ==
        "writeTexture region (0, 0) 64x1, layers 0+1 is outside texture 'Atlas' (32x16 at mip 1, 1 layers)");
    CHECK(failureOf([&] { validateWriteTexture(atlas, {.mipLevel = 3, .width = 1, .height = 1}, 4, 0); }) ==
          "writeTexture of texture 'Atlas' at mip 3; it has 3 mip levels");
    CHECK(failureOf([&] {
              validateWriteTexture(tiles, {.arrayLayer = 3, .layerCount = 2, .width = 1, .height = 1}, 2, 0);
          }) ==
          "writeTexture region (0, 0) 1x1, layers 3+2 is outside texture 'Tiles' (16x16 at mip 0, 4 layers)");
    CHECK(failureOf([&] { validateWriteTexture(atlas, {.width = 0, .height = 4}, 0, 0); }) ==
          "writeTexture of texture 'Atlas' has an empty region 0x4, 1 layers");
    CHECK(failureOf([&] { validateWriteTexture(atlas, {.width = 4, .height = 4}, 63, 0); }) ==
          "writeTexture to texture 'Atlas' needs 64 bytes for 4x4, 1 layers, and got 63");
    CHECK(failureOf([&] { validateWriteTexture(atlas, {.width = 4, .height = 4}, 1024, 12); }) ==
          "writeTexture to texture 'Atlas': 12 bytes per row for 4 texels of 4 bytes; it must hold a row and "
          "be "
          "a multiple of the texel size");
    CHECK(failureOf([&] { validateWriteTexture(atlas, {.width = 4, .height = 4}, 1024, 18); }) ==
          "writeTexture to texture 'Atlas': 18 bytes per row for 4 texels of 4 bytes; it must hold a row and "
          "be "
          "a multiple of the texel size");
    CHECK(failureOf([&] {
              validateWriteTexture(
                  {.format = Format::Rgba8Unorm, .usage = TextureUsage::Sampled, .debugName = "T"},
                  {.width = 1, .height = 1}, 4, 0);
          }) == "writeTexture to texture 'T', created without TextureUsage::CopyDst");
    CHECK(failureOf([&] {
              validateWriteTexture({.format = Format::Depth32Float,
                                    .usage = TextureUsage::DepthStencil | TextureUsage::CopyDst,
                                    .debugName = "Depth"},
                                   {.width = 1, .height = 1}, 4, 0);
          }) == "writeTexture to depth texture 'Depth'; depth textures cannot be uploaded in v1");
}

TEST_CASE("validateGenerateMipmaps", "[rhi][validation]") {
    validateGenerateMipmaps({.format = Format::Rgba8Srgb, .width = 64, .height = 64, .mipLevels = kAllMips});
    validateGenerateMipmaps({.format = Format::Rgba16Float, .width = 4, .height = 1, .mipLevels = 3});

    CHECK(failureOf([] { validateGenerateMipmaps({.width = 64, .height = 64, .debugName = "T"}); }) ==
          "generateMipmaps of texture 'T', which has one mip level");
    CHECK(
        failureOf([] {
            validateGenerateMipmaps(
                {.width = 4, .height = 4, .mipLevels = 2, .usage = TextureUsage::Sampled, .debugName = "T"});
        }) == "generateMipmaps of texture 'T', created without TextureUsage::CopyDst");
    CHECK(failureOf([] {
              validateGenerateMipmaps(
                  {.format = Format::R32Float, .width = 4, .height = 4, .mipLevels = 2, .debugName = "T"});
          }) == "generateMipmaps of texture 'T': format R32Float is not filterable and renderable");
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
    // The format support table: 32-bit floats render but do not blend.
    validateGraphicsPipelineDesc({.vertexShader = kShader, .targets = {.colorFormats = {Format::R32Float}}});
    CHECK(failureOf([] {
              validateGraphicsPipelineDesc({.vertexShader = kShader,
                                            .colorBlend = {{.enabled = true}},
                                            .targets = {.colorFormats = {Format::R32Float}},
                                            .debugName = "P"});
          }) == "Graphics pipeline 'P': color target 0 blends format R32Float, which cannot be blended");
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
