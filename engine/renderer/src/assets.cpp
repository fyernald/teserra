#include <teserra/renderer/assets.hpp>

#include <teserra/rhi/shader_blob.hpp>
#include <teserra/rhi/texture.hpp>

#include <teserra_renderer/shaders.hpp>

#include <array>
#include <memory>
#include <vector>

namespace teserra::renderer {

namespace {

// The texels of the fallback texture: magenta and black checks, magenta at the top left.
std::vector<std::byte> checkerboardTexels() {
    std::vector<std::byte> texels;
    texels.reserve(usize{kFallbackTextureSize} * kFallbackTextureSize * 4);
    for (u32 y = 0; y < kFallbackTextureSize; ++y) {
        for (u32 x = 0; x < kFallbackTextureSize; ++x) {
            const bool magenta = (x / kFallbackCheckSize + y / kFallbackCheckSize) % 2 == 0;
            const std::byte red = magenta ? std::byte{255} : std::byte{0};
            texels.insert(texels.end(), {red, std::byte{0}, red, std::byte{255}});
        }
    }
    return texels;
}

Result<Texture> createFallbackTexture(rhi::Device& device) {
    TESERRA_TRY_ASSIGN(const rhi::TextureHandle texture, device.createTexture({
                                                             .format = rhi::Format::Rgba8Srgb,
                                                             .width = kFallbackTextureSize,
                                                             .height = kFallbackTextureSize,
                                                             .debugName = "Fallback texture",
                                                         }));
    device.writeTexture(texture, {.width = kFallbackTextureSize, .height = kFallbackTextureSize},
                        checkerboardTexels());
    return Texture(device, texture, UVec2{kFallbackTextureSize});
}

Result<Shader> createFallbackShader(rhi::Device& device, rhi::ShaderStage stage,
                                    std::span<const std::byte> blob, std::string_view name) {
    TESERRA_TRY_ASSIGN(const rhi::ShaderModuleHandle module,
                       device.createShaderModule({.stage = stage, .blob = blob, .debugName = name}));
    return Shader(device, module, stage);
}

} // namespace

std::span<const std::string_view> TextureLoader::extensions() const {
    static constexpr std::array<std::string_view, 1> kExtensions{"png"};
    return kExtensions;
}

Result<Texture> TextureLoader::load(assets::AssetLoadContext& context, std::span<const std::byte> bytes) {
    TESERRA_TRY_ASSIGN(const assets::ImageData image, assets::decodeImage(bytes, m_config));
    TESERRA_TRY_ASSIGN(const rhi::TextureHandle texture, m_device->createTexture({
                                                             .format = rhi::Format::Rgba8Srgb,
                                                             .width = image.width,
                                                             .height = image.height,
                                                             .debugName = context.path(),
                                                         }));
    m_device->writeTexture(texture, {.width = image.width, .height = image.height}, image.pixels);
    return Texture(*m_device, texture, UVec2{image.width, image.height});
}

std::span<const std::string_view> ShaderLoader::extensions() const {
    static constexpr std::array<std::string_view, 1> kExtensions{"tshader"};
    return kExtensions;
}

Result<Shader> ShaderLoader::load(assets::AssetLoadContext& context, std::span<const std::byte> bytes) {
    // The module needs its stage up front; the blob knows it.
    TESERRA_TRY_ASSIGN(const rhi::ShaderBlobContents blob, rhi::readShaderBlob(bytes));
    TESERRA_TRY_ASSIGN(const rhi::ShaderModuleHandle module, m_device->createShaderModule({
                                                                 .stage = blob.stage,
                                                                 .blob = bytes,
                                                                 .debugName = context.path(),
                                                             }));
    return Shader(*m_device, module, blob.stage);
}

Result<void> registerAssetLoaders(assets::AssetRegistry& registry, rhi::Device& device) {
    // Every fallback first, so that a failure registers nothing.
    TESERRA_TRY_ASSIGN(Texture texture, createFallbackTexture(device));
    TESERRA_TRY_ASSIGN(Shader vertex, createFallbackShader(device, rhi::ShaderStage::Vertex,
                                                           shaders::kErrorVert, "Fallback vertex shader"));
    TESERRA_TRY_ASSIGN(Shader fragment,
                       createFallbackShader(device, rhi::ShaderStage::Fragment, shaders::kErrorFrag,
                                            "Fallback fragment shader"));
    registry.registerLoader<Texture>(std::make_unique<TextureLoader>(device));
    registry.registerLoader<Shader>(std::make_unique<ShaderLoader>(device));
    registry.setFallback(std::move(texture));
    registry.setFallback(std::move(vertex), ".vert.tshader");
    registry.setFallback(std::move(fragment), ".frag.tshader");
    return {};
}

} // namespace teserra::renderer
