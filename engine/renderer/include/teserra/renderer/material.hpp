#pragma once

// Materials (docs/architecture/renderer.md#materials-shaders-and-uniforms): a Material is a vertex and a
// fragment shader, pipeline state and the layout of its parameters, which comes from the shaders'
// reflection of set 2 (rhi::kMaterialGroup); a MaterialInstance gives the parameters values and textures and
// owns the bind group of set 2. Neither knows what draws with it: sprites, tilemaps and later meshes use the
// same types (3D readiness #7).
//
//   assets::Asset<Material> glow = registry.load<Material>("assets://glow.material.json");
//   auto instance = *MaterialInstance::create(device, glow);
//   instance.set("tint", Vec4{1.0F, 0.5F, 0.0F, 1.0F});
//   sprite.material = &instance;

#include <teserra/assets/asset_registry.hpp>
#include <teserra/core/math.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/renderer/assets.hpp>
#include <teserra/rhi/binding.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/handles.hpp>
#include <teserra/rhi/pipeline.hpp>
#include <teserra/rhi/sampler.hpp>
#include <teserra/rhi/shader_blob.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace teserra::renderer {

/// How a material's color combines with what is behind it.
enum class BlendMode : u8 {
    /// Replaces what is behind it.
    Opaque,
    /// Straight alpha: color * alpha + destination * (1 - alpha).
    Alpha,
    /// Color already multiplied by alpha: color + destination * (1 - alpha).
    Premultiplied,
    /// Color * alpha added to the destination.
    Additive,
};

/// The pipeline state a material draws with; part of the key of its pipelines.
struct MaterialState {
    BlendMode blend = BlendMode::Alpha;
    bool depthTest = false;
    bool depthWrite = false;
    rhi::CullMode cullMode = rhi::CullMode::None;

    friend bool operator==(const MaterialState&, const MaterialState&) = default;
};

/// The color blend state of `mode`, the same for every material and the sprite pass.
[[nodiscard]] rhi::BlendState blendState(BlendMode mode) noexcept;

/// A parameter: a member of the material's parameter block.
struct MaterialParameter {
    std::string name;
    /// Bytes from the start of the block, std140.
    u32 offset = 0;
    u32 size = 0;
    rhi::ShaderScalarType scalarType = rhi::ShaderScalarType::Float;
    /// Components of a vector or matrix column, 1 to 4.
    u32 rows = 1;
    /// 1 for scalars and vectors, 2 to 4 for matrices.
    u32 columns = 1;
    /// 0 for a member that is not an array.
    u32 arraySize = 0;
};

/// A texture of the material: a sampled texture of set 2.
struct MaterialTextureSlot {
    std::string name;
    u32 binding = 0;
};

/// What set 2 of a material's shaders holds: at most one uniform block, the parameters, and textures.
struct MaterialLayout {
    /// The binding of the parameter block; nothing without one.
    std::optional<u32> blockBinding;
    /// The block's std140 size, rounded up to 16 bytes.
    u32 blockSize = 0;
    std::vector<MaterialParameter> parameters;
    /// Sorted by binding.
    std::vector<MaterialTextureSlot> textures;
    /// The stages that use set 2.
    rhi::ShaderStage visibility = rhi::ShaderStage::None;

    /// The layout of set 2 of `stages` (the reflection of each shader): their bindings at set 2 merged.
    /// Fails (InvalidData) if set 2 holds a storage buffer or more than one uniform block, or if two stages
    /// disagree about a binding.
    [[nodiscard]] static Result<MaterialLayout>
    fromReflection(std::span<const rhi::ShaderReflection* const> stages);

    [[nodiscard]] const MaterialParameter* parameter(std::string_view name) const noexcept;
    [[nodiscard]] const MaterialTextureSlot* texture(std::string_view name) const noexcept;
    [[nodiscard]] bool empty() const noexcept { return !blockBinding && textures.empty(); }
};

/// A shader pair with its state and parameter layout. Created from Shader assets, which it keeps loaded,
/// or loaded from a `.material.json` file (MaterialLoader).
class Material {
public:
    /// What a material is made of.
    struct Desc {
        assets::Asset<Shader> vertex;
        assets::Asset<Shader> fragment;
        MaterialState state{};
        /// How the material's textures are filtered.
        rhi::Filter filter = rhi::Filter::Linear;
        /// Textures that instances start with, by slot name. Parameter defaults are zero; setDefault()
        /// changes them.
        std::vector<std::pair<std::string, assets::Asset<Texture>>> textures{};
        std::string_view debugName{};
    };

    /// Builds the parameter layout from the shaders' reflection, and creates the bind group layout of set 2
    /// and the sampler of its textures. The shaders must be ready (Asset::isReady()). Fails if the layout is
    /// invalid (MaterialLayout::fromReflection()), a texture names no slot, or a GPU object cannot be
    /// created.
    [[nodiscard]] static Result<Material> create(rhi::Device& device, Desc desc);

    Material(Material&&) noexcept = default;
    Material& operator=(Material&&) noexcept = default;
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;
    ~Material() = default;

    /// The shader modules and state pipelines of this material are made of.
    [[nodiscard]] rhi::ShaderModuleHandle vertexShader() const { return m_vertex.get().handle(); }
    [[nodiscard]] rhi::ShaderModuleHandle fragmentShader() const { return m_fragment.get().handle(); }
    [[nodiscard]] const MaterialState& state() const noexcept { return m_state; }
    /// Set 2 of the shaders: the parameter block and the texture slots.
    [[nodiscard]] const MaterialLayout& layout() const noexcept { return m_layout; }
    /// The layout of set 2; null for a material without parameters or textures.
    [[nodiscard]] rhi::BindGroupLayoutHandle groupLayout() const noexcept { return m_groupLayout.handle(); }
    [[nodiscard]] rhi::SamplerHandle sampler() const noexcept { return m_sampler.handle(); }
    /// The parameter block that instances start with.
    [[nodiscard]] std::span<const std::byte> defaults() const noexcept { return m_defaults; }
    /// The texture of each slot (layout().textures order) that instances start with; null handles for
    /// slots without one.
    [[nodiscard]] std::span<const rhi::TextureHandle> defaultTextures() const noexcept {
        return m_defaultTextureHandles;
    }
    /// A number unique to this material among the live ones, for sort keys.
    [[nodiscard]] u32 id() const noexcept { return m_id; }
    [[nodiscard]] const std::string& name() const noexcept { return m_name; }

    /// Sets the default of parameter `name` (see MaterialInstance::set()); false if there is no such
    /// parameter or its type differs.
    template <typename T>
    bool setDefault(std::string_view name, const T& value);

private:
    friend class MaterialLoader; // sets the defaults from JSON

    Material() = default;

    assets::Asset<Shader> m_vertex;
    assets::Asset<Shader> m_fragment;
    MaterialState m_state;
    MaterialLayout m_layout;
    detail::OwnedGpuObject<rhi::BindGroupLayoutHandle> m_groupLayout;
    detail::OwnedGpuObject<rhi::SamplerHandle> m_sampler;
    std::vector<std::byte> m_defaults;
    std::vector<assets::Asset<Texture>> m_defaultTextures;
    std::vector<rhi::TextureHandle> m_defaultTextureHandles;
    u32 m_id = 0;
    std::string m_name;
};

/// Writes `value` into a parameter block at `parameter`, if its type matches: float, int, uint, vec2, vec3,
/// vec4 (also from a Color) or mat4. An array takes its first element. False if the type differs or the
/// block is too small.
[[nodiscard]] bool writeParameter(const MaterialParameter& parameter, std::span<std::byte> block, f32 value);
[[nodiscard]] bool writeParameter(const MaterialParameter& parameter, std::span<std::byte> block, i32 value);
[[nodiscard]] bool writeParameter(const MaterialParameter& parameter, std::span<std::byte> block, u32 value);
[[nodiscard]] bool writeParameter(const MaterialParameter& parameter, std::span<std::byte> block,
                                  const Vec2& value);
[[nodiscard]] bool writeParameter(const MaterialParameter& parameter, std::span<std::byte> block,
                                  const Vec3& value);
[[nodiscard]] bool writeParameter(const MaterialParameter& parameter, std::span<std::byte> block,
                                  const Vec4& value);
[[nodiscard]] bool writeParameter(const MaterialParameter& parameter, std::span<std::byte> block,
                                  const Color& value);
[[nodiscard]] bool writeParameter(const MaterialParameter& parameter, std::span<std::byte> block,
                                  const Mat4& value);

/// A material with parameter values and textures: the uniform buffer of its parameter block and the bind
/// group of set 2. Changing a parameter marks the block dirty; flush() writes it before a draw uses the
/// instance. Created in code, where the game can change it, or loaded from a `.material-instance.json` file
/// (MaterialInstanceLoader).
class MaterialInstance {
public:
    /// Starts with the material's defaults. `material` must be ready and stay alive with the instance.
    [[nodiscard]] static Result<MaterialInstance> create(rhi::Device& device,
                                                         assets::Asset<Material> material);

    MaterialInstance(MaterialInstance&&) noexcept = default;
    MaterialInstance& operator=(MaterialInstance&&) noexcept = default;
    MaterialInstance(const MaterialInstance&) = delete;
    MaterialInstance& operator=(const MaterialInstance&) = delete;
    ~MaterialInstance() = default;

    /// The material this instance gives values to.
    [[nodiscard]] const Material& material() const { return m_material.get(); }

    /// Sets parameter `name`: f32, i32, u32, Vec2, Vec3, Vec4, Color or Mat4, matching the shader's type.
    /// False (and nothing changes) if there is no such parameter or its type differs.
    template <typename T>
    bool set(std::string_view name, const T& value) {
        const MaterialParameter* parameter = material().layout().parameter(name);
        if (parameter == nullptr || !writeParameter(*parameter, m_block, value)) {
            return false;
        }
        m_dirty = true;
        return true;
    }
    /// Shows `texture` in slot `name`; the instance keeps the asset loaded. False if there is no such slot,
    /// or the bind group cannot be recreated (logged).
    bool setTexture(std::string_view name, assets::Asset<Texture> texture);

    /// The parameter block as it is on the CPU.
    [[nodiscard]] std::span<const std::byte> block() const noexcept { return m_block; }
    /// Writes the parameter block to its buffer if it changed. Passes call it before drawing with the
    /// instance; the device orders the write before the frame's draws.
    void flush() const;
    /// The bind group of set 2; null for a material without parameters or textures.
    [[nodiscard]] rhi::BindGroupHandle group() const noexcept { return m_group.handle(); }

private:
    friend class MaterialInstanceLoader; // sets parameters from JSON

    MaterialInstance() = default;

    [[nodiscard]] Result<void> createGroup();

    rhi::Device* m_device = nullptr;
    assets::Asset<Material> m_material;
    std::vector<std::byte> m_block;
    mutable bool m_dirty = false;
    detail::OwnedGpuObject<rhi::BufferHandle> m_buffer;
    std::vector<assets::Asset<Texture>> m_textureAssets;
    std::vector<rhi::TextureHandle> m_textures;
    detail::OwnedGpuObject<rhi::BindGroupHandle> m_group;
};

/// Loads Material assets from `.material.json` files:
///
///     {
///       "vertex": "assets://shaders/glow.vert.tshader",      // required
///       "fragment": "assets://shaders/glow.frag.tshader",    // required
///       "blend": "alpha",           // opaque | alpha | premultiplied | additive
///       "depthTest": false, "depthWrite": false,
///       "cull": "none",             // none | front | back
///       "filter": "linear",         // nearest | linear, for the textures
///       "parameters": {"tint": [1, 0.5, 0, 1], "speed": 2},
///       "textures": {"noise": "assets://noise.png"}
///     }
///
/// Shaders and textures load as dependencies. Numbers fill scalars, arrays of 2 to 4 numbers vectors and
/// arrays of 16 numbers (column by column) matrices. Errors (InvalidData) name the field.
class MaterialLoader final : public assets::AssetLoader<Material> {
public:
    explicit MaterialLoader(rhi::Device& device) noexcept
        : m_device(&device) {}

    /// {"json"}: asset types are part of the key, so other JSON assets have their own loaders.
    [[nodiscard]] std::span<const std::string_view> extensions() const override;
    [[nodiscard]] Result<Material> load(assets::AssetLoadContext& context,
                                        std::span<const std::byte> bytes) override;

private:
    rhi::Device* m_device;
};

/// Loads MaterialInstance assets from `.material-instance.json` files: {"material": "<path>", "parameters":
/// {...}, "textures": {...}}, with parameters and textures as in MaterialLoader, over the material's
/// defaults.
class MaterialInstanceLoader final : public assets::AssetLoader<MaterialInstance> {
public:
    explicit MaterialInstanceLoader(rhi::Device& device) noexcept
        : m_device(&device) {}

    [[nodiscard]] std::span<const std::string_view> extensions() const override;
    [[nodiscard]] Result<MaterialInstance> load(assets::AssetLoadContext& context,
                                                std::span<const std::byte> bytes) override;

private:
    rhi::Device* m_device;
};

template <typename T>
bool Material::setDefault(std::string_view name, const T& value) {
    const MaterialParameter* parameter = m_layout.parameter(name);
    return parameter != nullptr && writeParameter(*parameter, m_defaults, value);
}

} // namespace teserra::renderer
