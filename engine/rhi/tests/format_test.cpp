#include <teserra/rhi/format.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <set>
#include <string_view>

using namespace teserra;
using namespace teserra::rhi;

TEST_CASE("formatInfo: color formats", "[rhi][format]") {
    CHECK(formatInfo(Format::Rgba8Unorm).name == "Rgba8Unorm");
    CHECK(formatInfo(Format::Rgba8Unorm).bytesPerTexel == 4);
    CHECK(formatInfo(Format::Rgba8Unorm).componentCount == 4);
    CHECK_FALSE(formatInfo(Format::Rgba8Unorm).isSrgb);
    CHECK(formatInfo(Format::Rgba8Srgb).isSrgb);
    CHECK(formatInfo(Format::Bgra8Srgb).isSrgb);
    CHECK(formatInfo(Format::R8Unorm).bytesPerTexel == 1);
    CHECK(formatInfo(Format::Rgba16Float).bytesPerTexel == 8);
    CHECK(formatInfo(Format::Rgba32Float).bytesPerTexel == 16);
    CHECK_FALSE(isDepthStencil(Format::Rgba16Float));
}

TEST_CASE("formatInfo: depth-stencil formats", "[rhi][format]") {
    CHECK(isDepthStencil(Format::Depth16Unorm));
    CHECK(isDepthStencil(Format::Depth32Float));
    CHECK_FALSE(formatInfo(Format::Depth32Float).hasStencil);

    const FormatInfo& d24s8 = formatInfo(Format::Depth24UnormStencil8);
    CHECK(d24s8.hasDepth);
    CHECK(d24s8.hasStencil);
    CHECK(d24s8.bytesPerTexel == 4);
    CHECK(formatInfo(Format::Depth32FloatStencil8).hasStencil);
}

TEST_CASE("formatInfo: Undefined has no texels", "[rhi][format]") {
    CHECK(formatInfo(Format::Undefined).name == "Undefined");
    CHECK(formatInfo(Format::Undefined).bytesPerTexel == 0);
    CHECK_FALSE(isDepthStencil(Format::Undefined));
}

TEST_CASE("formatInfo: every format has a unique name and a size", "[rhi][format]") {
    std::set<std::string_view> names;
    for (usize index = 0; index < kFormatCount; ++index) {
        const FormatInfo& info = formatInfo(static_cast<Format>(index));
        CHECK(names.insert(info.name).second);
        if (index != 0) { // Undefined
            CHECK(info.bytesPerTexel > 0);
            CHECK(info.componentCount > 0);
        }
    }
}

TEST_CASE("formatInfo: the format support table", "[rhi][format]") {
    using enum FormatCapabilities;
    // 8-bit, 10-bit and half-float color: everything a sprite or render target needs.
    for (const Format format : {Format::R8Unorm, Format::Rgba8Unorm, Format::Rgba8Srgb, Format::Bgra8Srgb,
                                Format::Rgb10A2Unorm, Format::Rgba16Float}) {
        INFO(formatInfo(format).name);
        CHECK(formatSupports(format, Sampled | Filterable | RenderTarget | Blendable));
        CHECK_FALSE(formatSupports(format, DepthStencil));
    }
    // 32-bit floats and integers: neither filtered nor blended.
    for (const Format format : {Format::R32Float, Format::Rgba32Float, Format::R32Uint}) {
        INFO(formatInfo(format).name);
        CHECK(formatSupports(format, Sampled | RenderTarget | Storage));
        CHECK_FALSE(formatSupports(format, Filterable));
        CHECK_FALSE(formatSupports(format, Blendable));
    }
    // Storage only where Vulkan requires it.
    CHECK(formatSupports(Format::Rgba8Unorm, Storage));
    CHECK_FALSE(formatSupports(Format::Rgba8Srgb, Storage));
    // Depth: sampled and attached, never a color target.
    for (const Format format : {Format::Depth16Unorm, Format::Depth32Float, Format::Depth32FloatStencil8}) {
        INFO(formatInfo(format).name);
        CHECK(formatSupports(format, Sampled | DepthStencil));
        CHECK_FALSE(formatSupports(format, RenderTarget));
    }
    CHECK(formatInfo(Format::Undefined).capabilities == None);
}

TEST_CASE("vertexFormatInfo: sizes and components", "[rhi][format]") {
    CHECK(vertexFormatInfo(VertexFormat::Float).sizeInBytes == 4);
    CHECK(vertexFormatInfo(VertexFormat::Float3).sizeInBytes == 12);
    CHECK(vertexFormatInfo(VertexFormat::Float3).componentCount == 3);
    CHECK(vertexFormatInfo(VertexFormat::Float4).sizeInBytes == 16);
    CHECK(vertexFormatInfo(VertexFormat::Unorm8x4).sizeInBytes == 4);
    CHECK(vertexFormatInfo(VertexFormat::Unorm8x4).componentCount == 4);
    CHECK(vertexFormatInfo(VertexFormat::Uint16x2).name == "Uint16x2");

    std::set<std::string_view> names;
    for (usize index = 0; index < kVertexFormatCount; ++index) {
        CHECK(names.insert(vertexFormatInfo(static_cast<VertexFormat>(index)).name).second);
    }
}

TEST_CASE("indexSize: bytes per index", "[rhi][format]") {
    STATIC_CHECK(indexSize(IndexType::Uint16) == 2);
    STATIC_CHECK(indexSize(IndexType::Uint32) == 4);
}
