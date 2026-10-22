#pragma once

#include <teserra/core/flags.hpp>
#include <teserra/core/types.hpp>

#include <cstddef>
#include <span>
#include <string_view>

namespace teserra::rhi {

/// A programmable pipeline stage. Also used as a mask of stages, e.g. which stages see a binding.
enum class ShaderStage : u8 {
    None = 0,
    Vertex = 1U << 0U,
    Fragment = 1U << 1U,
};
TESERRA_FLAGS(ShaderStage)

/// Creation parameters of a shader module: one compiled stage.
struct ShaderModuleDesc {
    ShaderStage stage = ShaderStage::Vertex;
    /// A .tshader blob produced by teserra_shaderc (ADR-0017). Only read during creation.
    std::span<const std::byte> blob{};
    std::string_view entryPoint = "main";
    /// Shown by graphics debuggers, in validation messages and in compile errors.
    std::string_view debugName{};
};

} // namespace teserra::rhi
