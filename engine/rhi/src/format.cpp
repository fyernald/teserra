#include <teserra/rhi/format.hpp>

#include <teserra/core/assert.hpp>

#include <array>

namespace teserra::rhi {

namespace {

struct FormatEntry {
    Format format;
    FormatInfo info;
};

// The capability sets of the format support table.
constexpr FormatCapabilities kColor = FormatCapabilities::Sampled | FormatCapabilities::Filterable |
                                      FormatCapabilities::RenderTarget | FormatCapabilities::Blendable;
constexpr FormatCapabilities kColorStorage = kColor | FormatCapabilities::Storage;
// 32-bit floats and integers: Vulkan guarantees neither filtering nor blending.
constexpr FormatCapabilities kUnfiltered =
    FormatCapabilities::Sampled | FormatCapabilities::RenderTarget | FormatCapabilities::Storage;
constexpr FormatCapabilities kDepth = FormatCapabilities::Sampled | FormatCapabilities::DepthStencil;

// One entry per Format, in enum order; the static_assert below rejects gaps and reordering.
constexpr std::array kFormats{
    FormatEntry{Format::Undefined, {.name = "Undefined"}},
    FormatEntry{Format::R8Unorm,
                {.name = "R8Unorm", .bytesPerTexel = 1, .componentCount = 1, .capabilities = kColor}},
    FormatEntry{Format::Rg8Unorm,
                {.name = "Rg8Unorm", .bytesPerTexel = 2, .componentCount = 2, .capabilities = kColor}},
    FormatEntry{
        Format::Rgba8Unorm,
        {.name = "Rgba8Unorm", .bytesPerTexel = 4, .componentCount = 4, .capabilities = kColorStorage}},
    FormatEntry{Format::Rgba8Srgb,
                {.name = "Rgba8Srgb",
                 .bytesPerTexel = 4,
                 .componentCount = 4,
                 .isSrgb = true,
                 .capabilities = kColor}},
    FormatEntry{Format::Bgra8Unorm,
                {.name = "Bgra8Unorm", .bytesPerTexel = 4, .componentCount = 4, .capabilities = kColor}},
    FormatEntry{Format::Bgra8Srgb,
                {.name = "Bgra8Srgb",
                 .bytesPerTexel = 4,
                 .componentCount = 4,
                 .isSrgb = true,
                 .capabilities = kColor}},
    FormatEntry{Format::Rgb10A2Unorm,
                {.name = "Rgb10A2Unorm", .bytesPerTexel = 4, .componentCount = 4, .capabilities = kColor}},
    FormatEntry{Format::R16Float,
                {.name = "R16Float", .bytesPerTexel = 2, .componentCount = 1, .capabilities = kColor}},
    FormatEntry{Format::Rg16Float,
                {.name = "Rg16Float", .bytesPerTexel = 4, .componentCount = 2, .capabilities = kColor}},
    FormatEntry{
        Format::Rgba16Float,
        {.name = "Rgba16Float", .bytesPerTexel = 8, .componentCount = 4, .capabilities = kColorStorage}},
    FormatEntry{Format::R32Float,
                {.name = "R32Float", .bytesPerTexel = 4, .componentCount = 1, .capabilities = kUnfiltered}},
    FormatEntry{Format::Rg32Float,
                {.name = "Rg32Float", .bytesPerTexel = 8, .componentCount = 2, .capabilities = kUnfiltered}},
    FormatEntry{
        Format::Rgba32Float,
        {.name = "Rgba32Float", .bytesPerTexel = 16, .componentCount = 4, .capabilities = kUnfiltered}},
    FormatEntry{Format::R32Uint,
                {.name = "R32Uint", .bytesPerTexel = 4, .componentCount = 1, .capabilities = kUnfiltered}},
    FormatEntry{Format::Depth16Unorm,
                {.name = "Depth16Unorm",
                 .bytesPerTexel = 2,
                 .componentCount = 1,
                 .hasDepth = true,
                 .capabilities = kDepth}},
    FormatEntry{Format::Depth24UnormStencil8,
                {.name = "Depth24UnormStencil8",
                 .bytesPerTexel = 4,
                 .componentCount = 2,
                 .hasDepth = true,
                 .hasStencil = true,
                 .capabilities = kDepth}},
    FormatEntry{Format::Depth32Float,
                {.name = "Depth32Float",
                 .bytesPerTexel = 4,
                 .componentCount = 1,
                 .hasDepth = true,
                 .capabilities = kDepth}},
    // A 32-bit float, 8 stencil bits and 24 unused bits, as copies see it.
    FormatEntry{Format::Depth32FloatStencil8,
                {.name = "Depth32FloatStencil8",
                 .bytesPerTexel = 8,
                 .componentCount = 2,
                 .hasDepth = true,
                 .hasStencil = true,
                 .capabilities = kDepth}},
};

template <typename Entries>
constexpr bool inEnumOrder(const Entries& entries) {
    for (usize index = 0; index < entries.size(); ++index) {
        if (static_cast<usize>(entries[index].format) != index) {
            return false;
        }
    }
    return true;
}

static_assert(kFormats.size() == kFormatCount, "every Format needs an entry in kFormats");
static_assert(inEnumOrder(kFormats), "kFormats must list the formats in enum order");

struct VertexFormatEntry {
    VertexFormat format;
    VertexFormatInfo info;
};

constexpr std::array kVertexFormats{
    VertexFormatEntry{VertexFormat::Float, {.name = "Float", .sizeInBytes = 4, .componentCount = 1}},
    VertexFormatEntry{VertexFormat::Float2, {.name = "Float2", .sizeInBytes = 8, .componentCount = 2}},
    VertexFormatEntry{VertexFormat::Float3, {.name = "Float3", .sizeInBytes = 12, .componentCount = 3}},
    VertexFormatEntry{VertexFormat::Float4, {.name = "Float4", .sizeInBytes = 16, .componentCount = 4}},
    VertexFormatEntry{VertexFormat::Unorm8x4, {.name = "Unorm8x4", .sizeInBytes = 4, .componentCount = 4}},
    VertexFormatEntry{VertexFormat::Uint8x4, {.name = "Uint8x4", .sizeInBytes = 4, .componentCount = 4}},
    VertexFormatEntry{VertexFormat::Uint16x2, {.name = "Uint16x2", .sizeInBytes = 4, .componentCount = 2}},
    VertexFormatEntry{VertexFormat::Uint32, {.name = "Uint32", .sizeInBytes = 4, .componentCount = 1}},
};

static_assert(kVertexFormats.size() == kVertexFormatCount, "every VertexFormat needs an entry");
static_assert(inEnumOrder(kVertexFormats), "kVertexFormats must list the formats in enum order");

} // namespace

const FormatInfo& formatInfo(Format format) {
    const auto index = static_cast<usize>(format);
    TESERRA_ASSERT(index < kFormats.size(), "Invalid Format value {}", index);
    return kFormats[index].info;
}

const VertexFormatInfo& vertexFormatInfo(VertexFormat format) {
    const auto index = static_cast<usize>(format);
    TESERRA_ASSERT(index < kVertexFormats.size(), "Invalid VertexFormat value {}", index);
    return kVertexFormats[index].info;
}

} // namespace teserra::rhi
