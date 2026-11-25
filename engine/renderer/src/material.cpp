#include <teserra/renderer/material.hpp>

#include <teserra/assets/json.hpp>
#include <teserra/core/log.hpp>
#include <teserra/rhi/buffer.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <string>
#include <utility>

namespace teserra::renderer {

namespace {

Error invalidData(std::string message) {
    return Error{.code = ErrorCode::InvalidData, .module = Module::Renderer, .message = std::move(message)};
}

// A number unique among live materials, for sort keys; it wraps after 4 billion materials.
u32 nextMaterialId() noexcept {
    static u32 next = 0;
    return ++next;
}

// Copies `count` 32-bit values into `block` at `parameter`, if the parameter is of `type` with `rows` rows
// and `columns` columns.
bool writeValues(const MaterialParameter& parameter, std::span<std::byte> block, rhi::ShaderScalarType type,
                 u32 rows, u32 columns, const void* values, usize bytes) {
    if (parameter.scalarType != type || parameter.rows != rows || parameter.columns != columns ||
        parameter.offset > block.size() || block.size() - parameter.offset < bytes) {
        return false;
    }
    std::memcpy(block.data() + parameter.offset, values, bytes);
    return true;
}

std::optional<BlendMode> blendFromName(std::string_view name) noexcept {
    if (name == "opaque") {
        return BlendMode::Opaque;
    }
    if (name == "alpha") {
        return BlendMode::Alpha;
    }
    if (name == "premultiplied") {
        return BlendMode::Premultiplied;
    }
    if (name == "additive") {
        return BlendMode::Additive;
    }
    return std::nullopt;
}

std::optional<rhi::CullMode> cullFromName(std::string_view name) noexcept {
    if (name == "none") {
        return rhi::CullMode::None;
    }
    if (name == "front") {
        return rhi::CullMode::Front;
    }
    if (name == "back") {
        return rhi::CullMode::Back;
    }
    return std::nullopt;
}

// Writes a JSON value into a parameter block: a number for scalars, an array of numbers for vectors and
// matrices (column by column).
Result<void> writeJsonParameter(const MaterialParameter& parameter, std::span<std::byte> block,
                                std::string_view name, const assets::JsonValue& value) {
    std::array<f64, 16> numbers{};
    usize count = 0;
    bool numeric = true;
    if (value.isNumber()) {
        numbers[0] = *value.asNumber();
        count = 1;
    } else if (value.isArray() && value.size() <= numbers.size()) {
        value.forEach([&](const assets::JsonValue& element) {
            numeric = numeric && element.isNumber();
            numbers[count++] = element.asNumber().value_or(0.0);
        });
    } else {
        numeric = false;
    }
    const u32 expected = parameter.rows * parameter.columns;
    if (!numeric || count != expected) {
        return Unexpected(invalidData(
            std::format("Parameter '{}' takes {} number{}", name, expected, expected == 1 ? "" : "s")));
    }
    bool written = false;
    switch (parameter.scalarType) {
        case rhi::ShaderScalarType::Float:
            if (parameter.columns == 4 && parameter.rows == 4) {
                Mat4 matrix{1.0F};
                for (usize i = 0; i < 16; ++i) {
                    matrix[static_cast<int>(i / 4)][static_cast<int>(i % 4)] = static_cast<f32>(numbers[i]);
                }
                written = writeParameter(parameter, block, matrix);
            } else if (parameter.columns == 1) {
                const Vec4 vector{static_cast<f32>(numbers[0]), static_cast<f32>(numbers[1]),
                                  static_cast<f32>(numbers[2]), static_cast<f32>(numbers[3])};
                written = parameter.rows == 1   ? writeParameter(parameter, block, vector.x)
                          : parameter.rows == 2 ? writeParameter(parameter, block, Vec2{vector})
                          : parameter.rows == 3 ? writeParameter(parameter, block, Vec3{vector})
                                                : writeParameter(parameter, block, vector);
            }
            break;
        case rhi::ShaderScalarType::Int:
            written = parameter.rows == 1 && writeParameter(parameter, block, static_cast<i32>(numbers[0]));
            break;
        case rhi::ShaderScalarType::Uint:
            written = parameter.rows == 1 && numbers[0] >= 0.0 &&
                      writeParameter(parameter, block, static_cast<u32>(numbers[0]));
            break;
    }
    if (!written) {
        return Unexpected(
            invalidData(std::format("Parameter '{}' has a type materials cannot set from JSON", name)));
    }
    return {};
}

// Applies the "parameters" and "textures" objects of a material or instance file: `setParameter(name,
// value)` and `setTexture(name, asset)` report unknown names.
template <typename SetParameter, typename SetTexture>
Result<void> applyOverrides(assets::AssetLoadContext& context, const assets::JsonValue& root,
                            SetParameter&& setParameter, SetTexture&& setTexture) {
    const assets::JsonValue parameters = root["parameters"];
    if (parameters.exists() && !parameters.isObject()) {
        return Unexpected(invalidData("'parameters' must be an object"));
    }
    Result<void> result;
    parameters.forEachMember([&](std::string_view name, const assets::JsonValue& value) {
        result = setParameter(name, value);
        return result.has_value();
    });
    TESERRA_TRY(result);
    const assets::JsonValue textures = root["textures"];
    if (textures.exists() && !textures.isObject()) {
        return Unexpected(invalidData("'textures' must be an object"));
    }
    textures.forEachMember([&](std::string_view name, const assets::JsonValue& value) {
        const std::optional<std::string_view> path = value.asString();
        if (!path) {
            result = Unexpected(invalidData(std::format("Texture '{}' must be a path", name)));
            return false;
        }
        Result<std::string> resolved = assets::resolveRelativePath(context.path(), *path);
        if (!resolved) {
            result = Unexpected(std::move(resolved).error());
            return false;
        }
        result = setTexture(name, context.load<Texture>(*resolved));
        return result.has_value();
    });
    return result;
}

Result<assets::JsonDocument> parseObject(std::span<const std::byte> bytes, std::string_view what) {
    Result<assets::JsonDocument> document = assets::JsonDocument::parse(bytes);
    if (!document) {
        return Unexpected(Error{
            .code = document.error().code, .module = Module::Renderer, .message = document.error().message});
    }
    if (!document->root().isObject()) {
        return Unexpected(
            invalidData(std::format("A {} file holds an object, not {}", what, document->root().typeName())));
    }
    return document;
}

} // namespace

rhi::BlendState blendState(BlendMode mode) noexcept {
    using rhi::BlendFactor;
    rhi::BlendState state{.enabled = mode != BlendMode::Opaque};
    switch (mode) {
        case BlendMode::Opaque:
            return state;
        case BlendMode::Alpha:
            state.srcColor = BlendFactor::SrcAlpha;
            state.dstColor = BlendFactor::OneMinusSrcAlpha;
            break;
        case BlendMode::Premultiplied:
            state.srcColor = BlendFactor::One;
            state.dstColor = BlendFactor::OneMinusSrcAlpha;
            break;
        case BlendMode::Additive:
            state.srcColor = BlendFactor::SrcAlpha;
            state.dstColor = BlendFactor::One;
            break;
    }
    // Alpha accumulates coverage the same way in every blended mode.
    state.srcAlpha = BlendFactor::One;
    state.dstAlpha = BlendFactor::OneMinusSrcAlpha;
    return state;
}

// MaterialLayout

Result<MaterialLayout> MaterialLayout::fromReflection(std::span<const rhi::ShaderReflection* const> stages) {
    MaterialLayout layout;
    // What each binding of set 2 is, to check that the stages agree.
    std::array<const rhi::ShaderBinding*, rhi::kMaxBindingsPerGroup> seen{};
    for (usize stage = 0; stage < stages.size(); ++stage) {
        if (stages[stage] == nullptr) {
            continue;
        }
        bool usesSet = false;
        for (const rhi::ShaderBinding& binding : stages[stage]->bindings) {
            if (binding.group != rhi::kMaterialGroup) {
                continue;
            }
            usesSet = true;
            if (const rhi::ShaderBinding* other = seen[binding.binding]; other != nullptr) {
                if (other->type != binding.type || other->size != binding.size) {
                    return Unexpected(invalidData(
                        std::format("The shaders disagree about set 2, binding {} ('{}' and '{}')",
                                    binding.binding, other->name, binding.name)));
                }
                continue;
            }
            seen[binding.binding] = &binding;
            switch (binding.type) {
                case rhi::BindingType::UniformBuffer:
                    if (layout.blockBinding) {
                        return Unexpected(invalidData(std::format(
                            "Set 2 has two uniform blocks ('{}' at binding {}); a material has one parameter "
                            "block",
                            binding.name, binding.binding)));
                    }
                    layout.blockBinding = binding.binding;
                    layout.blockSize = binding.size;
                    for (const rhi::ShaderBlockMember& member : binding.members) {
                        layout.parameters.push_back({.name = member.name,
                                                     .offset = member.offset,
                                                     .size = member.size,
                                                     .scalarType = member.scalarType,
                                                     .rows = member.rows,
                                                     .columns = member.columns,
                                                     .arraySize = member.arraySize});
                    }
                    break;
                case rhi::BindingType::SampledTexture:
                    layout.textures.push_back({.name = binding.name, .binding = binding.binding});
                    break;
                case rhi::BindingType::StorageBuffer:
                    return Unexpected(invalidData(std::format(
                        "Set 2 has a storage buffer ('{}'), which materials do not support", binding.name)));
            }
        }
        if (usesSet) {
            layout.visibility |= stage == 0 ? rhi::ShaderStage::Vertex : rhi::ShaderStage::Fragment;
        }
    }
    std::ranges::sort(layout.textures, {}, &MaterialTextureSlot::binding);
    return layout;
}

const MaterialParameter* MaterialLayout::parameter(std::string_view name) const noexcept {
    const auto found = std::ranges::find(parameters, name, &MaterialParameter::name);
    return found != parameters.end() ? &*found : nullptr;
}

const MaterialTextureSlot* MaterialLayout::texture(std::string_view name) const noexcept {
    const auto found = std::ranges::find(textures, name, &MaterialTextureSlot::name);
    return found != textures.end() ? &*found : nullptr;
}

// Parameters

bool writeParameter(const MaterialParameter& parameter, std::span<std::byte> block, f32 value) {
    return writeValues(parameter, block, rhi::ShaderScalarType::Float, 1, 1, &value, sizeof(value));
}
bool writeParameter(const MaterialParameter& parameter, std::span<std::byte> block, i32 value) {
    return writeValues(parameter, block, rhi::ShaderScalarType::Int, 1, 1, &value, sizeof(value));
}
bool writeParameter(const MaterialParameter& parameter, std::span<std::byte> block, u32 value) {
    return writeValues(parameter, block, rhi::ShaderScalarType::Uint, 1, 1, &value, sizeof(value));
}
bool writeParameter(const MaterialParameter& parameter, std::span<std::byte> block, const Vec2& value) {
    const std::array values{value.x, value.y};
    return writeValues(parameter, block, rhi::ShaderScalarType::Float, 2, 1, values.data(), sizeof(values));
}
bool writeParameter(const MaterialParameter& parameter, std::span<std::byte> block, const Vec3& value) {
    const std::array values{value.x, value.y, value.z};
    return writeValues(parameter, block, rhi::ShaderScalarType::Float, 3, 1, values.data(), sizeof(values));
}
bool writeParameter(const MaterialParameter& parameter, std::span<std::byte> block, const Vec4& value) {
    const std::array values{value.x, value.y, value.z, value.w};
    return writeValues(parameter, block, rhi::ShaderScalarType::Float, 4, 1, values.data(), sizeof(values));
}
bool writeParameter(const MaterialParameter& parameter, std::span<std::byte> block, const Color& value) {
    return writeParameter(parameter, block, Vec4{value.r, value.g, value.b, value.a});
}
bool writeParameter(const MaterialParameter& parameter, std::span<std::byte> block, const Mat4& value) {
    // std140 stores a mat4 as four vec4 columns, as glm does.
    std::array<f32, 16> values{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            values[(static_cast<usize>(column) * 4) + static_cast<usize>(row)] = value[column][row];
        }
    }
    return writeValues(parameter, block, rhi::ShaderScalarType::Float, 4, 4, values.data(), sizeof(values));
}

// Material

Result<Material> Material::create(rhi::Device& device, Desc desc) {
    if (!desc.vertex.isReady() || !desc.fragment.isReady()) {
        return Unexpected(invalidData(std::format("Material '{}' needs loaded shaders", desc.debugName)));
    }
    if (desc.vertex.get().stage() != rhi::ShaderStage::Vertex ||
        desc.fragment.get().stage() != rhi::ShaderStage::Fragment) {
        return Unexpected(
            invalidData(std::format("Material '{}' needs a vertex and a fragment shader", desc.debugName)));
    }
    const std::array<const rhi::ShaderReflection*, 2> stages{&desc.vertex.get().reflection(),
                                                             &desc.fragment.get().reflection()};
    Result<MaterialLayout> layout = MaterialLayout::fromReflection(stages);
    if (!layout) {
        return Unexpected(
            invalidData(std::format("Material '{}': {}", desc.debugName, layout.error().message)));
    }

    Material material;
    material.m_name = std::string(desc.debugName);
    material.m_vertex = std::move(desc.vertex);
    material.m_fragment = std::move(desc.fragment);
    material.m_state = desc.state;
    material.m_layout = std::move(*layout);
    material.m_defaults.assign(material.m_layout.blockSize, std::byte{0});
    material.m_defaultTextures.resize(material.m_layout.textures.size());
    material.m_defaultTextureHandles.resize(material.m_layout.textures.size());
    material.m_id = nextMaterialId();
    for (auto& [name, texture] : desc.textures) {
        const auto slot = std::ranges::find(material.m_layout.textures, name, &MaterialTextureSlot::name);
        if (slot == material.m_layout.textures.end()) {
            return Unexpected(
                invalidData(std::format("Material '{}' has no texture slot '{}'", desc.debugName, name)));
        }
        const auto index = static_cast<usize>(slot - material.m_layout.textures.begin());
        material.m_defaultTextureHandles[index] =
            texture.isReady() ? texture.get().handle() : rhi::TextureHandle{};
        material.m_defaultTextures[index] = std::move(texture);
    }

    if (!material.m_layout.empty()) {
        std::vector<rhi::BindGroupLayoutEntry> entries;
        if (material.m_layout.blockBinding) {
            entries.push_back({.binding = *material.m_layout.blockBinding,
                               .type = rhi::BindingType::UniformBuffer,
                               .visibility = material.m_layout.visibility});
        }
        for (const MaterialTextureSlot& slot : material.m_layout.textures) {
            entries.push_back({.binding = slot.binding,
                               .type = rhi::BindingType::SampledTexture,
                               .visibility = material.m_layout.visibility});
        }
        TESERRA_TRY_ASSIGN(const rhi::BindGroupLayoutHandle groupLayout,
                           device.createBindGroupLayout({.entries = entries, .debugName = desc.debugName}));
        material.m_groupLayout = detail::OwnedGpuObject(device, groupLayout);
    }
    if (!material.m_layout.textures.empty()) {
        TESERRA_TRY_ASSIGN(const rhi::SamplerHandle sampler,
                           device.createSampler({.minFilter = desc.filter,
                                                 .magFilter = desc.filter,
                                                 .mipmapMode = rhi::MipmapMode::Nearest,
                                                 .debugName = desc.debugName}));
        material.m_sampler = detail::OwnedGpuObject(device, sampler);
    }
    return material;
}

// MaterialInstance

Result<MaterialInstance> MaterialInstance::create(rhi::Device& device, assets::Asset<Material> material) {
    if (!material.isReady()) {
        return Unexpected(invalidData("A material instance needs a loaded material"));
    }
    MaterialInstance instance;
    instance.m_device = &device;
    instance.m_material = std::move(material);
    const Material& base = instance.m_material.get();
    instance.m_block.assign(base.defaults().begin(), base.defaults().end());
    instance.m_textureAssets.resize(base.layout().textures.size());
    instance.m_textures.assign(base.defaultTextures().begin(), base.defaultTextures().end());
    if (base.layout().blockBinding) {
        TESERRA_TRY_ASSIGN(
            const rhi::BufferHandle buffer,
            device.createBuffer({.sizeInBytes = base.layout().blockSize,
                                 .usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst,
                                 .debugName = base.name()}));
        instance.m_buffer = detail::OwnedGpuObject(device, buffer);
        instance.m_dirty = true;
    }
    TESERRA_TRY(instance.createGroup());
    return instance;
}

Result<void> MaterialInstance::createGroup() {
    const Material& base = material();
    // A slot without a texture leaves the instance without a group until setTexture() fills it.
    if (base.layout().empty() || std::ranges::find(m_textures, rhi::TextureHandle{}) != m_textures.end()) {
        m_group = {};
        return {};
    }
    std::vector<rhi::BindGroupEntry> entries;
    if (base.layout().blockBinding) {
        entries.push_back({.binding = *base.layout().blockBinding,
                           .buffer = {.buffer = m_buffer.handle(), .size = base.layout().blockSize}});
    }
    for (usize slot = 0; slot < m_textures.size(); ++slot) {
        entries.push_back({.binding = base.layout().textures[slot].binding,
                           .texture = m_textures[slot],
                           .sampler = base.sampler()});
    }
    TESERRA_TRY_ASSIGN(const rhi::BindGroupHandle group,
                       m_device->createBindGroup(
                           {.layout = base.groupLayout(), .entries = entries, .debugName = base.name()}));
    // The device keeps the previous group until the frames that use it are done.
    m_group = detail::OwnedGpuObject(*m_device, group);
    return {};
}

bool MaterialInstance::setTexture(std::string_view name, assets::Asset<Texture> texture) {
    const std::vector<MaterialTextureSlot>& slots = material().layout().textures;
    const auto slot = std::ranges::find(slots, name, &MaterialTextureSlot::name);
    if (slot == slots.end() || !texture.isReady()) {
        return false;
    }
    const auto index = static_cast<usize>(slot - slots.begin());
    m_textures[index] = texture.get().handle();
    m_textureAssets[index] = std::move(texture);
    if (Result<void> created = createGroup(); !created) {
        TESERRA_LOG_ERROR(LogCategory::Renderer, "Could not recreate the bind group of material '{}': {}",
                          material().name(), created.error());
        return false;
    }
    return true;
}

void MaterialInstance::flush() const {
    if (m_dirty && !m_buffer.handle().isNull()) {
        m_device->writeBuffer(m_buffer.handle(), 0, m_block);
    }
    m_dirty = false;
}

// Loaders

std::span<const std::string_view> MaterialLoader::extensions() const {
    static constexpr std::array<std::string_view, 1> kExtensions{"json"};
    return kExtensions;
}

Result<Material> MaterialLoader::load(assets::AssetLoadContext& context, std::span<const std::byte> bytes) {
    TESERRA_TRY_ASSIGN(const assets::JsonDocument document, parseObject(bytes, "material"));
    const assets::JsonValue root = document.root();
    const auto shader = [&](std::string_view field) -> Result<assets::Asset<Shader>> {
        const std::optional<std::string_view> path = root[field].asString();
        if (!path) {
            return Unexpected(
                invalidData(std::format("'{}' must be a path, not {}", field, root[field].typeName())));
        }
        TESERRA_TRY_ASSIGN(const std::string resolved, assets::resolveRelativePath(context.path(), *path));
        assets::Asset<Shader> loaded = context.load<Shader>(resolved);
        if (!loaded.isReady()) {
            return Unexpected(invalidData(std::format("'{}': '{}' did not load", field, resolved)));
        }
        return loaded;
    };
    // Every member is named: Clang's -Wmissing-designated-field-initializers (in -Wextra) flags the rest.
    Material::Desc desc{.vertex = {}, .fragment = {}, .debugName = context.path()};
    TESERRA_TRY_ASSIGN(desc.vertex, shader("vertex"));
    TESERRA_TRY_ASSIGN(desc.fragment, shader("fragment"));
    if (root["blend"].exists()) {
        const std::optional<BlendMode> blend = blendFromName(root["blend"].asString().value_or(""));
        if (!blend) {
            return Unexpected(invalidData("'blend' must be opaque, alpha, premultiplied or additive"));
        }
        desc.state.blend = *blend;
    }
    if (root["cull"].exists()) {
        const std::optional<rhi::CullMode> cull = cullFromName(root["cull"].asString().value_or(""));
        if (!cull) {
            return Unexpected(invalidData("'cull' must be none, front or back"));
        }
        desc.state.cullMode = *cull;
    }
    for (const auto& [field, target] :
         {std::pair{"depthTest", &desc.state.depthTest}, std::pair{"depthWrite", &desc.state.depthWrite}}) {
        if (root[field].exists()) {
            const std::optional<bool> value = root[field].asBool();
            if (!value) {
                return Unexpected(invalidData(std::format("'{}' must be true or false", field)));
            }
            *target = *value;
        }
    }
    if (root["filter"].exists()) {
        const std::optional<std::string_view> filter = root["filter"].asString();
        if (filter != "nearest" && filter != "linear") {
            return Unexpected(invalidData("'filter' must be nearest or linear"));
        }
        desc.filter = filter == "nearest" ? rhi::Filter::Nearest : rhi::Filter::Linear;
    }

    // The textures go into the Desc, the parameters into the created material's defaults.
    std::vector<std::pair<std::string, assets::JsonValue>> parameters;
    TESERRA_TRY(applyOverrides(
        context, root,
        [&](std::string_view name, const assets::JsonValue& value) -> Result<void> {
            parameters.emplace_back(std::string(name), value);
            return {};
        },
        [&](std::string_view name, assets::Asset<Texture> texture) -> Result<void> {
            desc.textures.emplace_back(std::string(name), std::move(texture));
            return {};
        }));
    TESERRA_TRY_ASSIGN(Material material, Material::create(*m_device, std::move(desc)));
    for (const auto& [name, value] : parameters) {
        const MaterialParameter* parameter = material.layout().parameter(name);
        if (parameter == nullptr) {
            return Unexpected(invalidData(std::format("The material has no parameter '{}'", name)));
        }
        TESERRA_TRY(writeJsonParameter(*parameter, material.m_defaults, name, value));
    }
    return material;
}

std::span<const std::string_view> MaterialInstanceLoader::extensions() const {
    static constexpr std::array<std::string_view, 1> kExtensions{"json"};
    return kExtensions;
}

Result<MaterialInstance> MaterialInstanceLoader::load(assets::AssetLoadContext& context,
                                                      std::span<const std::byte> bytes) {
    TESERRA_TRY_ASSIGN(const assets::JsonDocument document, parseObject(bytes, "material instance"));
    const assets::JsonValue root = document.root();
    const std::optional<std::string_view> path = root["material"].asString();
    if (!path) {
        return Unexpected(
            invalidData(std::format("'material' must be a path, not {}", root["material"].typeName())));
    }
    TESERRA_TRY_ASSIGN(const std::string resolved, assets::resolveRelativePath(context.path(), *path));
    assets::Asset<Material> material = context.load<Material>(resolved);
    if (!material.isReady()) {
        return Unexpected(invalidData(std::format("'material': '{}' did not load", resolved)));
    }
    TESERRA_TRY_ASSIGN(MaterialInstance instance, MaterialInstance::create(*m_device, std::move(material)));
    TESERRA_TRY(applyOverrides(
        context, root,
        [&](std::string_view name, const assets::JsonValue& value) -> Result<void> {
            const MaterialParameter* parameter = instance.material().layout().parameter(name);
            if (parameter == nullptr) {
                return Unexpected(invalidData(std::format("The material has no parameter '{}'", name)));
            }
            instance.m_dirty = true;
            return writeJsonParameter(*parameter, instance.m_block, name, value);
        },
        [&](std::string_view name, assets::Asset<Texture> texture) -> Result<void> {
            if (!instance.setTexture(name, std::move(texture))) {
                return Unexpected(invalidData(std::format("The material has no texture slot '{}'", name)));
            }
            return {};
        }));
    return instance;
}

} // namespace teserra::renderer
