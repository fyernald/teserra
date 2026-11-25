#include <teserra/renderer/assets.hpp>

#include <teserra/renderer/material.hpp>
#include <teserra/renderer/sprite_sheet.hpp>
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
    Result<rhi::ShaderBlobContents> contents = rhi::readShaderBlob(blob);
    return Shader(device, module, stage,
                  contents ? std::move(contents->reflection) : rhi::ShaderReflection{});
}

} // namespace

Result<Texture> Texture::create(rhi::Device& device, UVec2 size, std::span<const std::byte> texels,
                                rhi::Format format, std::string_view debugName) {
    const u64 expected = rhi::textureDataSize(format, {.width = size.x, .height = size.y}, 0);
    if (size.x == 0 || size.y == 0 || texels.size() != expected) {
        return fail(ErrorCode::InvalidArgument, Module::Renderer,
                    "Texture '{}' of {}x{} texels needs {} bytes, not {}", debugName, size.x, size.y,
                    expected, texels.size());
    }
    TESERRA_TRY_ASSIGN(
        const rhi::TextureHandle handle,
        device.createTexture({.format = format, .width = size.x, .height = size.y, .debugName = debugName}));
    device.writeTexture(handle, {.width = size.x, .height = size.y}, texels);
    return Texture(device, handle, size);
}

std::span<const std::string_view> TextureLoader::extensions() const {
    static constexpr std::array<std::string_view, 1> kExtensions{"png"};
    return kExtensions;
}

Result<Texture> TextureLoader::load(assets::AssetLoadContext& context, std::span<const std::byte> bytes) {
    TESERRA_TRY_ASSIGN(const assets::ImageData image, assets::decodeImage(bytes, m_config));
    const assets::AssetImportSettings& settings = context.settings();
    const bool mipmaps = settings.mipmaps && (image.width > 1 || image.height > 1);
    TESERRA_TRY_ASSIGN(const rhi::TextureHandle texture,
                       m_device->createTexture({
                           .format = settings.srgb ? rhi::Format::Rgba8Srgb : rhi::Format::Rgba8Unorm,
                           .width = image.width,
                           .height = image.height,
                           .mipLevels = mipmaps ? rhi::kAllMips : 1,
                           .debugName = context.path(),
                       }));
    m_device->writeTexture(texture, {.width = image.width, .height = image.height}, image.pixels);
    if (mipmaps) {
        m_device->generateMipmaps(texture);
    }
    return Texture(*m_device, texture, UVec2{image.width, image.height});
}

std::span<const std::string_view> ShaderLoader::extensions() const {
    static constexpr std::array<std::string_view, 1> kExtensions{"tshader"};
    return kExtensions;
}

Result<Shader> ShaderLoader::load(assets::AssetLoadContext& context, std::span<const std::byte> bytes) {
    // The module needs its stage up front; the blob knows it.
    TESERRA_TRY_ASSIGN(rhi::ShaderBlobContents blob, rhi::readShaderBlob(bytes));
    TESERRA_TRY_ASSIGN(const rhi::ShaderModuleHandle module, m_device->createShaderModule({
                                                                 .stage = blob.stage,
                                                                 .blob = bytes,
                                                                 .debugName = context.path(),
                                                             }));
    return Shader(*m_device, module, blob.stage, std::move(blob.reflection));
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
    registry.registerLoader<SpriteSheet>(std::make_unique<SpriteSheetLoader>());
    registry.registerLoader<Material>(std::make_unique<MaterialLoader>(device));
    registry.registerLoader<MaterialInstance>(std::make_unique<MaterialInstanceLoader>(device));
    registry.setFallback(std::move(texture));
    registry.setFallback(std::move(vertex), ".vert.tshader");
    registry.setFallback(std::move(fragment), ".frag.tshader");
    return {};
}

} // namespace teserra::renderer
