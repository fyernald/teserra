#pragma once

// GPU asset types and their loaders (docs/architecture/assets.md#loaders). The renderer owns them because
// it works on the rhi::Device; the asset system itself knows no GPU.
//
//   TESERRA_TRY(renderer::registerAssetLoaders(registry, device));
//   assets::Asset<renderer::Texture> hero = registry.load<renderer::Texture>("assets://sprites/hero.png");
//   sprite.texture = hero.get().handle();   // the magenta checkerboard if hero.png is missing or broken

#include <teserra/assets/asset_registry.hpp>
#include <teserra/assets/image.hpp>
#include <teserra/core/math.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/handles.hpp>
#include <teserra/rhi/shader.hpp>

#include <cstddef>
#include <span>
#include <string_view>
#include <utility>

namespace teserra::renderer {

namespace detail {

// Owns one GPU object of `device` and destroys it with itself. The device keeps the object on the GPU until
// no frame in flight uses it, so the asset registry may unload at the end of any frame.
template <typename Handle>
class OwnedGpuObject {
public:
    OwnedGpuObject(rhi::Device& device, Handle handle) noexcept
        : m_device(&device), m_handle(handle) {}
    ~OwnedGpuObject() {
        if (m_device != nullptr) {
            m_device->destroy(m_handle);
        }
    }

    OwnedGpuObject(OwnedGpuObject&& other) noexcept
        : m_device(std::exchange(other.m_device, nullptr)),
          m_handle(std::exchange(other.m_handle, Handle{})) {}
    OwnedGpuObject& operator=(OwnedGpuObject&& other) noexcept {
        OwnedGpuObject moved(std::move(other));
        std::swap(m_device, moved.m_device);
        std::swap(m_handle, moved.m_handle);
        return *this;
    }
    OwnedGpuObject(const OwnedGpuObject&) = delete;
    OwnedGpuObject& operator=(const OwnedGpuObject&) = delete;

    [[nodiscard]] Handle handle() const noexcept { return m_handle; }

private:
    rhi::Device* m_device;
    Handle m_handle;
};

} // namespace detail

/// A texture asset: a GPU texture, destroyed when the asset unloads.
class Texture {
public:
    /// Takes ownership of `handle`, a texture of `device` of `size` texels.
    Texture(rhi::Device& device, rhi::TextureHandle handle, UVec2 size) noexcept
        : m_texture(device, handle), m_size(size) {}

    [[nodiscard]] rhi::TextureHandle handle() const noexcept { return m_texture.handle(); }
    /// Width and height in texels.
    [[nodiscard]] UVec2 size() const noexcept { return m_size; }

private:
    detail::OwnedGpuObject<rhi::TextureHandle> m_texture;
    UVec2 m_size;
};

/// A shader asset: one compiled stage as a shader module, destroyed when the asset unloads.
class Shader {
public:
    /// Takes ownership of `handle`, a shader module of `device` for `stage`.
    Shader(rhi::Device& device, rhi::ShaderModuleHandle handle, rhi::ShaderStage stage) noexcept
        : m_module(device, handle), m_stage(stage) {}

    [[nodiscard]] rhi::ShaderModuleHandle handle() const noexcept { return m_module.handle(); }
    [[nodiscard]] rhi::ShaderStage stage() const noexcept { return m_stage; }

private:
    detail::OwnedGpuObject<rhi::ShaderModuleHandle> m_module;
    rhi::ShaderStage m_stage;
};

/// Loads Texture assets from PNG files: an Rgba8Srgb texture with one mip level, named after the path.
/// Import settings (linear color, mipmaps) come with the `.asset.json` sidecar.
class TextureLoader final : public assets::AssetLoader<Texture> {
public:
    /// `device` must outlive the loader and the textures it loads.
    explicit TextureLoader(rhi::Device& device, const assets::ImageDecodeConfig& config = {}) noexcept
        : m_device(&device), m_config(config) {}

    /// {"png"}
    [[nodiscard]] std::span<const std::string_view> extensions() const override;
    [[nodiscard]] Result<Texture> load(assets::AssetLoadContext& context,
                                       std::span<const std::byte> bytes) override;

private:
    rhi::Device* m_device;
    assets::ImageDecodeConfig m_config;
};

/// Loads Shader assets from .tshader blobs (ADR-0017): a shader module of the blob's stage, named after the
/// path.
class ShaderLoader final : public assets::AssetLoader<Shader> {
public:
    /// `device` must outlive the loader and the shaders it loads.
    explicit ShaderLoader(rhi::Device& device) noexcept
        : m_device(&device) {}

    /// {"tshader"}
    [[nodiscard]] std::span<const std::string_view> extensions() const override;
    [[nodiscard]] Result<Shader> load(assets::AssetLoadContext& context,
                                      std::span<const std::byte> bytes) override;

private:
    rhi::Device* m_device;
};

/// Side of the fallback texture in texels: magenta and black checks of kFallbackCheckSize texels.
inline constexpr u32 kFallbackTextureSize = 8;
inline constexpr u32 kFallbackCheckSize = 4;

/// Registers the renderer's loaders (TextureLoader, ShaderLoader, SpriteSheetLoader) for `device` with
/// `registry`, and creates the fallbacks of their failed loads once:
/// - Texture: kFallbackTextureSize² magenta and black checks (Rgba8Srgb), magenta at the top left.
/// - Shader, for paths ending with ".vert.tshader": a vertex shader without inputs whose triangles are
///   empty; ".frag.tshader": opaque magenta. teserra_add_shaders() names blobs after their sources, so the
///   stage is in the path. Other shader paths have no fallback.
/// `device` must outlive the registry. Fails if a fallback cannot be created on the device.
[[nodiscard]] Result<void> registerAssetLoaders(assets::AssetRegistry& registry, rhi::Device& device);

} // namespace teserra::renderer
