// Defaults of the RHI descriptors, and the rules they follow: designated initialization that names only
// what differs, and trivially copyable command payloads.

#include <teserra/rhi/buffer.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/pipeline.hpp>
#include <teserra/rhi/render_pass.hpp>
#include <teserra/rhi/sampler.hpp>
#include <teserra/rhi/shader.hpp>
#include <teserra/rhi/texture.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <type_traits>

using namespace teserra;
using namespace teserra::rhi;

// Copied into the command stream with memcpy (#26).
static_assert(std::is_trivially_copyable_v<RenderPassDesc>);
static_assert(std::is_trivially_copyable_v<ColorAttachment>);
static_assert(std::is_trivially_copyable_v<DepthStencilAttachment>);
static_assert(std::is_trivially_copyable_v<TextureHandle>);
static_assert(sizeof(TextureHandle) == 8);

TEST_CASE("BufferDesc: defaults", "[rhi][descriptor]") {
    constexpr BufferDesc kDesc{};
    CHECK(kDesc.sizeInBytes == 0);
    CHECK(kDesc.usage == BufferUsage::None);
    CHECK(kDesc.memory == MemoryLocation::GpuOnly);
    CHECK(kDesc.debugName.empty());
}

TEST_CASE("TextureDesc: defaults to a 1x1 sampled sRGB 2D texture", "[rhi][descriptor]") {
    constexpr TextureDesc kDesc{};
    CHECK(kDesc.dimension == TextureDimension::Tex2D);
    CHECK(kDesc.format == Format::Rgba8Srgb);
    CHECK(kDesc.width == 1);
    CHECK(kDesc.height == 1);
    CHECK(kDesc.depthOrLayers == 1);
    CHECK(kDesc.mipLevels == 1);
    CHECK(kDesc.sampleCount == 1);
    CHECK(kDesc.usage == (TextureUsage::Sampled | TextureUsage::CopyDst));
}

TEST_CASE("SamplerDesc: defaults to linear filtering, clamped, no comparison", "[rhi][descriptor]") {
    const SamplerDesc desc{};
    CHECK(desc.minFilter == Filter::Linear);
    CHECK(desc.magFilter == Filter::Linear);
    CHECK(desc.mipmapMode == MipmapMode::Linear);
    CHECK(desc.addressU == AddressMode::ClampToEdge);
    CHECK(desc.addressW == AddressMode::ClampToEdge);
    CHECK(desc.maxAnisotropy == 1.0F);
    CHECK_FALSE(desc.compare.has_value());
}

TEST_CASE("ShaderModuleDesc: defaults", "[rhi][descriptor]") {
    constexpr ShaderModuleDesc kDesc{};
    CHECK(kDesc.stage == ShaderStage::Vertex);
    CHECK(kDesc.blob.empty());
    CHECK(kDesc.entryPoint == "main");
}

TEST_CASE("GraphicsPipelineDesc: defaults to opaque triangles without depth", "[rhi][descriptor]") {
    const GraphicsPipelineDesc desc{};
    CHECK(desc.topology == PrimitiveTopology::TriangleList);
    CHECK(desc.raster.cullMode == CullMode::None);
    CHECK(desc.raster.frontFace == FrontFace::CounterClockwise); // the RHI convention (ADR-0024)
    CHECK(desc.raster.fillMode == FillMode::Fill);
    CHECK_FALSE(desc.depthStencil.depthTest);
    CHECK_FALSE(desc.depthStencil.depthWrite);
    CHECK(desc.depthStencil.depthCompare == CompareOp::Less);
    for (const BlendState& blend : desc.colorBlend) {
        CHECK_FALSE(blend.enabled);
        CHECK(blend.writeMask == ColorWriteMask::All);
    }
    for (const Format format : desc.targets.colorFormats) {
        CHECK(format == Format::Undefined);
    }
    CHECK(desc.targets.depthStencilFormat == Format::Undefined);
    CHECK(desc.targets.sampleCount == 1);
    CHECK(desc.vertexBuffers.empty());
    CHECK(desc.layout.isNull());
}

TEST_CASE("GraphicsPipelineDesc: designated initialization names only what differs", "[rhi][descriptor]") {
    constexpr std::array kBuffers{VertexBufferLayout{.stride = 20}};
    constexpr std::array kAttributes{
        VertexAttribute{.location = 0, .format = VertexFormat::Float3},
        VertexAttribute{.location = 1, .format = VertexFormat::Float2, .offset = 12},
    };

    const GraphicsPipelineDesc desc{
        .vertexBuffers = kBuffers,
        .vertexAttributes = kAttributes,
        .colorBlend = {{
            .enabled = true,
            .srcColor = BlendFactor::SrcAlpha,
            .dstColor = BlendFactor::OneMinusSrcAlpha,
        }},
        .targets = {.colorFormats = {Format::Rgba8Srgb}, .depthStencilFormat = Format::Depth32Float},
        .debugName = "Sprites",
    };

    CHECK(desc.vertexBuffers.size() == 1);
    CHECK(desc.vertexAttributes[1].offset == 12);
    CHECK(desc.colorBlend[0].enabled);
    CHECK(desc.colorBlend[0].dstColor == BlendFactor::OneMinusSrcAlpha);
    CHECK(desc.colorBlend[0].dstAlpha == BlendFactor::Zero); // untouched fields keep their defaults
    CHECK_FALSE(desc.colorBlend[1].enabled);
    CHECK(desc.targets.colorFormats[0] == Format::Rgba8Srgb);
    CHECK(desc.targets.colorFormats[1] == Format::Undefined);
    CHECK(desc.debugName == "Sprites");
}

TEST_CASE("RenderPassDesc: defaults to no attachments", "[rhi][descriptor]") {
    constexpr RenderPassDesc kDesc{};
    for (const ColorAttachment& attachment : kDesc.colorAttachments) {
        CHECK(attachment.texture.isNull());
        CHECK(attachment.loadOp == LoadOp::Load);
        CHECK(attachment.storeOp == StoreOp::Store);
    }
    CHECK(kDesc.depthStencil.texture.isNull());
    CHECK(kDesc.depthStencil.depthLoadOp == LoadOp::Clear);
    CHECK(kDesc.depthStencil.clearDepth == 1.0F);
}

TEST_CASE("RenderPassDesc: designated initialization of attachments", "[rhi][descriptor]") {
    const TextureHandle backbuffer{3, 1};
    const TextureHandle normals{4, 1};

    const RenderPassDesc desc{
        .colorAttachments = {{
                                 .texture = backbuffer,
                                 .loadOp = LoadOp::Clear,
                                 .clearColor = {0.1F, 0.2F, 0.3F, 1.0F},
                             },
                             {.texture = normals, .loadOp = LoadOp::DontCare}},
    };

    CHECK(desc.colorAttachments[0].texture == backbuffer);
    CHECK(desc.colorAttachments[0].clearColor.y == 0.2F);
    CHECK(desc.colorAttachments[0].storeOp == StoreOp::Store);
    CHECK(desc.colorAttachments[1].texture == normals);
    CHECK(desc.colorAttachments[1].loadOp == LoadOp::DontCare);
    CHECK(desc.colorAttachments[2].texture.isNull());
}

TEST_CASE("DeviceDesc: defaults to OpenGL with a vsynced sRGB swapchain", "[rhi][descriptor]") {
    constexpr DeviceDesc kDesc{};
    CHECK(kDesc.backend == Backend::OpenGL);
    CHECK(kDesc.enableValidation == (TESERRA_DEBUG_BUILD != 0));
    CHECK(kDesc.swapchain.format == Format::Rgba8Srgb);
    CHECK(kDesc.swapchain.presentMode == PresentMode::Fifo);
}

TEST_CASE("DeviceLimits: defaults are the guaranteed minimums", "[rhi][descriptor]") {
    constexpr DeviceLimits kLimits{};
    CHECK(kLimits.maxTextureDimension2D >= 8192);
    CHECK(kLimits.uniformBufferOffsetAlignment == 256);
    CHECK(kLimits.maxSamplerAnisotropy == 1.0F);
}

TEST_CASE("Usage flags combine within one kind", "[rhi][descriptor]") {
    const BufferUsage usage = BufferUsage::Vertex | BufferUsage::CopyDst;
    CHECK(hasAll(usage, BufferUsage::CopyDst));
    CHECK_FALSE(hasAny(usage, BufferUsage::Uniform | BufferUsage::Index));
    CHECK(hasAll(ShaderStage::Vertex | ShaderStage::Fragment, ShaderStage::Fragment));
    STATIC_CHECK(!std::is_convertible_v<BufferUsage, TextureUsage>);
}
