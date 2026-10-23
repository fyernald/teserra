#include <teserra/rhi/shader_blob.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <optional>

namespace teserra::rhi {

namespace {

// Format version 1 (docs/architecture/rhi.md#tshader-blobs). All values are little-endian u32s and every
// part starts at a multiple of 4 bytes.
static_assert(std::endian::native == std::endian::little, "the blob format is read and written in place");

constexpr std::array<std::byte, 4> kMagic{std::byte{'T'}, std::byte{'S'}, std::byte{'H'}, std::byte{'B'}};
constexpr std::size_t kHeaderSize = 16;           // magic, version, stage, section count
constexpr std::size_t kSectionEntrySize = 12;     // type, offset, size
constexpr std::size_t kReflectionHeaderSize = 12; // binding count, vertex input count, push-constant size
constexpr std::size_t kBindingRecordSize = 24;    // group, binding, type, size, name offset, name length
constexpr std::size_t kInputRecordSize = 20; // location, scalar type, components, name offset, name length

// Section types. Plain numbers rather than an enum: they come from untrusted data.
constexpr u32 kSpirvSection = 1;
constexpr u32 kGlslSection = 2;
constexpr u32 kReflectionSection = 3;

class Writer {
public:
    void u32Value(u32 value) {
        const auto bytes = std::bit_cast<std::array<std::byte, 4>>(value);
        m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
    }
    void bytes(std::span<const std::byte> data) { m_bytes.insert(m_bytes.end(), data.begin(), data.end()); }
    void text(std::string_view data) { bytes(std::as_bytes(std::span(data))); }
    void align() { m_bytes.resize((m_bytes.size() + 3) & ~std::size_t{3}, std::byte{0}); }
    void patch(std::size_t offset, u32 value) {
        const auto bytes = std::bit_cast<std::array<std::byte, 4>>(value);
        std::memcpy(m_bytes.data() + offset, bytes.data(), bytes.size());
    }
    [[nodiscard]] std::size_t size() const noexcept { return m_bytes.size(); }
    [[nodiscard]] std::vector<std::byte> take() && { return std::move(m_bytes); }

private:
    std::vector<std::byte> m_bytes;
};

std::vector<std::byte> reflectionSection(const ShaderReflection& reflection) {
    // Names go into one string area after the records; offsets are relative to its start.
    std::string names;
    const auto addName = [&names](std::string_view name) {
        const auto offset = static_cast<u32>(names.size());
        names += name;
        return offset;
    };

    Writer writer;
    writer.u32Value(static_cast<u32>(reflection.bindings.size()));
    writer.u32Value(static_cast<u32>(reflection.vertexInputs.size()));
    writer.u32Value(reflection.pushConstantSize);
    for (const ShaderBinding& binding : reflection.bindings) {
        writer.u32Value(binding.group);
        writer.u32Value(binding.binding);
        writer.u32Value(static_cast<u32>(binding.type));
        writer.u32Value(binding.size);
        writer.u32Value(addName(binding.name));
        writer.u32Value(static_cast<u32>(binding.name.size()));
    }
    for (const ShaderVertexInput& input : reflection.vertexInputs) {
        writer.u32Value(input.location);
        writer.u32Value(static_cast<u32>(input.scalarType));
        writer.u32Value(input.components);
        writer.u32Value(addName(input.name));
        writer.u32Value(static_cast<u32>(input.name.size()));
    }
    writer.text(names);
    writer.align();
    return std::move(writer).take();
}

// Bounds-checked little-endian reads from a byte range.
class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) noexcept
        : m_bytes(bytes) {}

    [[nodiscard]] std::optional<u32> u32At(std::size_t offset) const noexcept {
        if (offset > m_bytes.size() || m_bytes.size() - offset < 4) {
            return std::nullopt;
        }
        std::array<std::byte, 4> value{};
        std::memcpy(value.data(), m_bytes.data() + offset, value.size());
        return std::bit_cast<u32>(value);
    }

    // The range [offset, offset + size), or nothing if it does not lie inside.
    [[nodiscard]] std::optional<std::span<const std::byte>> range(u64 offset, u64 size) const noexcept {
        if (offset > m_bytes.size() || size > m_bytes.size() - offset) {
            return std::nullopt;
        }
        return m_bytes.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(size));
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_bytes.size(); }

private:
    std::span<const std::byte> m_bytes;
};

std::string_view asText(std::span<const std::byte> bytes) noexcept {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

bool isSingleStage(u32 stage) noexcept {
    return stage == static_cast<u32>(ShaderStage::Vertex) ||
           stage == static_cast<u32>(ShaderStage::Fragment) ||
           stage == static_cast<u32>(ShaderStage::Compute);
}

Result<ShaderReflection> readReflection(std::span<const std::byte> section) {
    const Reader reader(section);
    const std::optional<u32> bindingCount = reader.u32At(0);
    const std::optional<u32> inputCount = reader.u32At(4);
    const std::optional<u32> pushConstantSize = reader.u32At(8);
    if (!bindingCount || !inputCount || !pushConstantSize) {
        return fail(ErrorCode::InvalidData, Module::Rhi,
                    "The reflection section of the shader blob is truncated");
    }
    const u64 recordsSize = u64{*bindingCount} * kBindingRecordSize + u64{*inputCount} * kInputRecordSize;
    const std::optional<std::span<const std::byte>> records =
        reader.range(kReflectionHeaderSize, recordsSize);
    if (!records) {
        return fail(
            ErrorCode::InvalidData, Module::Rhi,
            "The reflection section of the shader blob is too short for {} bindings and {} vertex inputs",
            *bindingCount, *inputCount);
    }
    const Reader strings(section.subspan(kReflectionHeaderSize + static_cast<std::size_t>(recordsSize)));
    const auto name = [&strings](const Reader& record, std::size_t at) -> std::optional<std::string> {
        const std::optional<u32> offset = record.u32At(at);
        const std::optional<u32> length = record.u32At(at + 4);
        const std::optional<std::span<const std::byte>> text =
            offset && length ? strings.range(*offset, *length) : std::nullopt;
        return text ? std::optional<std::string>(asText(*text)) : std::nullopt;
    };

    ShaderReflection reflection{.pushConstantSize = *pushConstantSize};
    reflection.bindings.reserve(*bindingCount);
    for (u32 i = 0; i < *bindingCount; ++i) {
        const Reader record(records->subspan(i * kBindingRecordSize, kBindingRecordSize));
        const u32 group = record.u32At(0).value_or(0);
        const u32 binding = record.u32At(4).value_or(0);
        const u32 type = record.u32At(8).value_or(0);
        if (group >= kMaxBindGroups || binding >= kMaxBindingsPerGroup) {
            return fail(
                ErrorCode::InvalidData, Module::Rhi,
                "Binding {} of the shader blob is at group {}, binding {}; the limits are {} groups of {}", i,
                group, binding, kMaxBindGroups, kMaxBindingsPerGroup);
        }
        if (type > static_cast<u32>(BindingType::SampledTexture)) {
            return fail(ErrorCode::InvalidData, Module::Rhi,
                        "Binding {} of the shader blob has an invalid type {}", i, type);
        }
        std::optional<std::string> text = name(record, 16);
        if (!text) {
            return fail(ErrorCode::InvalidData, Module::Rhi,
                        "The name of binding {} lies outside the shader blob's reflection section", i);
        }
        reflection.bindings.push_back({.group = group,
                                       .binding = binding,
                                       .type = static_cast<BindingType>(type),
                                       .size = record.u32At(12).value_or(0),
                                       .name = std::move(*text)});
    }

    const std::span<const std::byte> inputRecords = records->subspan(*bindingCount * kBindingRecordSize);
    reflection.vertexInputs.reserve(*inputCount);
    for (u32 i = 0; i < *inputCount; ++i) {
        const Reader record(inputRecords.subspan(i * kInputRecordSize, kInputRecordSize));
        const u32 scalarType = record.u32At(4).value_or(0);
        const u32 components = record.u32At(8).value_or(0);
        if (scalarType > static_cast<u32>(ShaderScalarType::Uint) || components < 1 || components > 4) {
            return fail(ErrorCode::InvalidData, Module::Rhi,
                        "Vertex input {} of the shader blob has an invalid type ({}, {} components)", i,
                        scalarType, components);
        }
        std::optional<std::string> text = name(record, 12);
        if (!text) {
            return fail(ErrorCode::InvalidData, Module::Rhi,
                        "The name of vertex input {} lies outside the shader blob's reflection section", i);
        }
        reflection.vertexInputs.push_back({.location = record.u32At(0).value_or(0),
                                           .scalarType = static_cast<ShaderScalarType>(scalarType),
                                           .components = components,
                                           .name = std::move(*text)});
    }
    return reflection;
}

} // namespace

std::vector<std::byte> writeShaderBlob(const ShaderBlobContents& contents) {
    const std::vector<std::byte> reflection = reflectionSection(contents.reflection);
    const std::array<std::pair<u32, std::span<const std::byte>>, 3> sections{{
        {kSpirvSection, contents.spirv},
        {kGlslSection, std::as_bytes(std::span(contents.glsl))},
        {kReflectionSection, reflection},
    }};

    Writer writer;
    writer.bytes(kMagic);
    writer.u32Value(kShaderBlobVersion);
    writer.u32Value(static_cast<u32>(contents.stage));
    writer.u32Value(static_cast<u32>(sections.size()));
    const std::size_t table = writer.size();
    for (std::size_t i = 0; i < sections.size(); ++i) {
        writer.u32Value(0); // filled in below
        writer.u32Value(0);
        writer.u32Value(0);
    }
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const auto& [type, data] = sections[i];
        const std::size_t entry = table + i * kSectionEntrySize;
        writer.patch(entry, type);
        writer.patch(entry + 4, static_cast<u32>(writer.size()));
        writer.patch(entry + 8, static_cast<u32>(data.size()));
        writer.bytes(data);
        writer.align();
    }
    return std::move(writer).take();
}

Result<ShaderBlobContents> readShaderBlob(std::span<const std::byte> blob) {
    const Reader reader(blob);
    if (blob.size() < kHeaderSize) {
        return fail(ErrorCode::InvalidData, Module::Rhi,
                    "The shader blob is {} bytes, too short for a .tshader blob", blob.size());
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), blob.begin())) {
        return fail(ErrorCode::InvalidData, Module::Rhi, "The shader blob is not a .tshader blob");
    }
    const u32 version = reader.u32At(4).value_or(0);
    if (version != kShaderBlobVersion) {
        return fail(ErrorCode::Unsupported, Module::Rhi,
                    "The shader blob has format version {}; this build reads version {}", version,
                    kShaderBlobVersion);
    }
    const u32 stage = reader.u32At(8).value_or(0);
    if (!isSingleStage(stage)) {
        return fail(ErrorCode::InvalidData, Module::Rhi, "The shader blob has an invalid stage {}", stage);
    }
    const u32 sectionCount = reader.u32At(12).value_or(0);
    if (!reader.range(kHeaderSize, u64{sectionCount} * kSectionEntrySize)) {
        return fail(ErrorCode::InvalidData, Module::Rhi,
                    "The section table of the shader blob ({} sections) is truncated", sectionCount);
    }

    std::optional<std::span<const std::byte>> spirv;
    std::optional<std::span<const std::byte>> glsl;
    std::optional<std::span<const std::byte>> reflection;
    for (u32 i = 0; i < sectionCount; ++i) {
        const std::size_t entry = kHeaderSize + std::size_t{i} * kSectionEntrySize;
        const u32 type = reader.u32At(entry).value_or(0);
        const std::optional<std::span<const std::byte>> data =
            reader.range(reader.u32At(entry + 4).value_or(0), reader.u32At(entry + 8).value_or(0));
        if (!data) {
            return fail(ErrorCode::InvalidData, Module::Rhi,
                        "Section {} of the shader blob lies outside the blob", i);
        }
        std::optional<std::span<const std::byte>>* known = type == kSpirvSection        ? &spirv
                                                           : type == kGlslSection       ? &glsl
                                                           : type == kReflectionSection ? &reflection
                                                                                        : nullptr;
        if (known == nullptr) {
            continue; // a section of a later format revision
        }
        if (known->has_value()) {
            return fail(ErrorCode::InvalidData, Module::Rhi, "The shader blob has two sections of type {}",
                        type);
        }
        *known = *data;
    }

    if (!spirv || spirv->empty() || spirv->size() % 4 != 0) {
        return fail(ErrorCode::InvalidData, Module::Rhi, "The shader blob has no SPIR-V of whole words");
    }
    if (!reflection) {
        return fail(ErrorCode::InvalidData, Module::Rhi, "The shader blob has no reflection section");
    }
    TESERRA_TRY_ASSIGN(ShaderReflection parsed, readReflection(*reflection));
    return ShaderBlobContents{.stage = static_cast<ShaderStage>(stage),
                              .spirv = *spirv,
                              .glsl = glsl ? asText(*glsl) : std::string_view(),
                              .reflection = std::move(parsed)};
}

} // namespace teserra::rhi
