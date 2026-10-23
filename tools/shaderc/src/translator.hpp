#pragma once

// Steps 2 and 3 of ADR-0017: reflection of a compiled shader and GLSL for OpenGL with SPIRV-Cross.

#include <teserra/rhi/shader_blob.hpp>

#include "compiler.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace teserra::shaderc {

struct Translation {
    /// GLSL 450 core with `layout(binding = rhi::flatBinding(set, binding))`; empty when translation failed.
    std::string glsl{};
    rhi::ShaderReflection reflection{};
    /// Errors about resources the RHI cannot bind, named after the resource; they have no line, because
    /// SPIR-V does not carry line numbers.
    std::vector<Diagnostic> diagnostics{};

    [[nodiscard]] bool succeeded() const noexcept { return !glsl.empty(); }
};

/// Reflects `spirv` (from compileGlsl()) and emits its GLSL for OpenGL. `source` names the shader file in
/// diagnostics. `spirv` itself is not changed: the blob keeps its descriptor set decorations.
[[nodiscard]] Translation translateForOpenGl(std::span<const std::uint32_t> spirv, ShaderStage stage,
                                             const std::filesystem::path& source);

} // namespace teserra::shaderc
