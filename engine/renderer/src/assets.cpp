#include <teserra/renderer/assets.hpp>

#include <teserra/rhi/shader_blob.hpp>
#include <teserra/rhi/texture.hpp>

#include <array>
#include <memory>

namespace teserra::renderer {

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

void registerAssetLoaders(assets::AssetRegistry& registry, rhi::Device& device) {
    registry.registerLoader<Texture>(std::make_unique<TextureLoader>(device));
    registry.registerLoader<Shader>(std::make_unique<ShaderLoader>(device));
}

} // namespace teserra::renderer
