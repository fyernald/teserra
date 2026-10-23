#include "translator.hpp"

#include <spirv_glsl.hpp>

#include <algorithm>
#include <exception>
#include <optional>
#include <string_view>
#include <utility>

namespace teserra::shaderc {

namespace {

using rhi::BindingType;
using rhi::ShaderBinding;
using rhi::ShaderScalarType;
using rhi::ShaderVertexInput;

// std140 blocks are bound in ranges of whole 16-byte rows; GL and Vulkan reject a smaller range (#36).
constexpr std::uint32_t kUniformBlockAlignment = 16;

class Translator {
public:
    Translator(std::span<const std::uint32_t> spirv, ShaderStage stage, std::filesystem::path source)
        : m_compiler(spirv.data(), spirv.size()), m_stage(stage), m_source(std::move(source)) {}

    Translation run() {
        const spirv_cross::ShaderResources resources = m_compiler.get_shader_resources();
        rejectUnsupported(resources);
        for (const spirv_cross::Resource& buffer : resources.uniform_buffers) {
            addBinding(buffer, BindingType::UniformBuffer);
        }
        for (const spirv_cross::Resource& buffer : resources.storage_buffers) {
            addBinding(buffer, BindingType::StorageBuffer);
        }
        for (const spirv_cross::Resource& texture : resources.sampled_images) {
            addBinding(texture, BindingType::SampledTexture);
        }
        if (m_stage == ShaderStage::Vertex) {
            for (const spirv_cross::Resource& input : resources.stage_inputs) {
                addVertexInput(input);
            }
        }
        if (!m_result.diagnostics.empty()) {
            return std::move(m_result);
        }

        std::ranges::sort(m_result.reflection.bindings, {}, [](const ShaderBinding& binding) {
            return std::pair(binding.group, binding.binding);
        });
        std::ranges::sort(m_result.reflection.vertexInputs, {}, &ShaderVertexInput::location);
        for (auto it = m_result.reflection.bindings.begin(); it != m_result.reflection.bindings.end(); ++it) {
            const auto next = std::next(it);
            if (next != m_result.reflection.bindings.end() && next->group == it->group &&
                next->binding == it->binding) {
                error("'" + it->name + "' and '" + next->name + "' are both at set " +
                      std::to_string(it->group) + ", binding " + std::to_string(it->binding));
            }
        }
        if (!m_result.diagnostics.empty()) {
            return std::move(m_result);
        }

        spirv_cross::CompilerGLSL::Options options = m_compiler.get_common_options();
        options.version = 450;
        options.es = false;
        options.vulkan_semantics = false;
        options.enable_420pack_extension = true; // keeps layout(binding = N)
        // No clip-space fixup: the OpenGL backend uses glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE).
        options.vertex.fixup_clipspace = false;
        options.vertex.flip_vert_y = false;
        m_compiler.set_common_options(options);
        m_result.glsl = m_compiler.compile();
        return std::move(m_result);
    }

private:
    void error(std::string message) {
        m_result.diagnostics.push_back(
            {.severity = Severity::Error, .file = m_source, .message = std::move(message)});
    }

    void rejectUnsupported(const spirv_cross::ShaderResources& resources) {
        const auto reject = [this](const spirv_cross::SmallVector<spirv_cross::Resource>& found,
                                   std::string_view what) {
            for (const spirv_cross::Resource& resource : found) {
                // Blocks by their block name, as for buffers (SPIRV-Cross names push constants by instance).
                const std::string& block = m_compiler.get_name(resource.base_type_id);
                error("'" + (block.empty() ? resource.name : block) + "' is " + std::string(what));
            }
        };
        reject(resources.separate_images,
               "a separate texture; v1 binds textures combined with a sampler: use sampler2D");
        reject(resources.separate_samplers,
               "a separate sampler; v1 binds textures combined with a sampler: use sampler2D");
        reject(resources.storage_images, "a storage image, which v1 cannot bind");
        reject(resources.atomic_counters, "an atomic counter, which v1 cannot bind");
        reject(resources.subpass_inputs, "a subpass input, which v1 cannot bind");
        reject(resources.acceleration_structures, "an acceleration structure, which v1 cannot bind");
        reject(resources.push_constant_buffers,
               "a push-constant block; push constants are not supported yet (#135)");
    }

    void addBinding(const spirv_cross::Resource& resource, BindingType type) {
        const std::uint32_t set = m_compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
        const std::uint32_t binding = m_compiler.get_decoration(resource.id, spv::DecorationBinding);
        if (!m_compiler.get_type(resource.type_id).array.empty()) {
            error("'" + resource.name + "' is an array of resources, which v1 cannot bind");
            return;
        }
        if (set >= rhi::kMaxBindGroups || binding >= rhi::kMaxBindingsPerGroup) {
            error("'" + resource.name + "' is at set " + std::to_string(set) + ", binding " +
                  std::to_string(binding) + "; sets are 0 to " + std::to_string(rhi::kMaxBindGroups - 1) +
                  " and bindings 0 to " + std::to_string(rhi::kMaxBindingsPerGroup - 1));
            return;
        }

        std::uint32_t size = 0;
        if (type != BindingType::SampledTexture) {
            size = static_cast<std::uint32_t>(
                m_compiler.get_declared_struct_size(m_compiler.get_type(resource.base_type_id)));
            if (type == BindingType::UniformBuffer) {
                size = (size + kUniformBlockAlignment - 1) / kUniformBlockAlignment * kUniformBlockAlignment;
            }
        }
        m_result.reflection.bindings.push_back(
            {.group = set, .binding = binding, .type = type, .size = size, .name = resource.name});

        // Only SPIRV-Cross's copy of the module changes: the blob keeps the original decorations.
        m_compiler.set_decoration(resource.id, spv::DecorationBinding, rhi::flatBinding(set, binding));
        m_compiler.unset_decoration(resource.id, spv::DecorationDescriptorSet);
    }

    void addVertexInput(const spirv_cross::Resource& input) {
        const spirv_cross::SPIRType& type = m_compiler.get_type(input.type_id);
        std::optional<ShaderScalarType> scalarType;
        if (type.width == 32) {
            switch (type.basetype) {
                case spirv_cross::SPIRType::Float:
                    scalarType = ShaderScalarType::Float;
                    break;
                case spirv_cross::SPIRType::Int:
                    scalarType = ShaderScalarType::Int;
                    break;
                case spirv_cross::SPIRType::UInt:
                    scalarType = ShaderScalarType::Uint;
                    break;
                default:
                    break;
            }
        }
        if (!scalarType || type.columns != 1 || !type.array.empty()) {
            error("vertex input '" + input.name +
                  "' is not a scalar or vector of 32-bit floats or integers, which v1 vertex layouts cannot "
                  "feed");
            return;
        }
        m_result.reflection.vertexInputs.push_back(
            {.location = m_compiler.get_decoration(input.id, spv::DecorationLocation),
             .scalarType = *scalarType,
             .components = type.vecsize,
             .name = input.name});
    }

    spirv_cross::CompilerGLSL m_compiler;
    ShaderStage m_stage;
    std::filesystem::path m_source;
    Translation m_result;
};

} // namespace

Translation translateForOpenGl(std::span<const std::uint32_t> spirv, ShaderStage stage,
                               const std::filesystem::path& source) {
    try {
        return Translator(spirv, stage, source).run();
    } catch (const std::exception& exception) { // spirv_cross::CompilerError
        Translation failed;
        failed.diagnostics.push_back({.severity = Severity::Error,
                                      .file = source,
                                      .message = std::string("SPIRV-Cross: ") + exception.what()});
        return failed;
    }
}

} // namespace teserra::shaderc
