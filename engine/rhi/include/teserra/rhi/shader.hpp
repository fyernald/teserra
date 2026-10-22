#pragma once

#include <teserra/core/flags.hpp>
#include <teserra/core/types.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

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

/// TEMPORARY, removed by #44 (ADR-0017): wraps GLSL 450 core source in a blob that ShaderModuleDesc::blob
/// accepts until teserra_shaderc produces .tshader blobs. Only the OpenGL backend compiles it (the null
/// backend stores any blob). The source is kept unchanged, so compile errors name its own line numbers; it
/// must not use `set` qualifiers.
[[nodiscard]] std::vector<std::byte> temporaryGlslBlob(std::string_view source);

/// TEMPORARY, removed by #44: the GLSL source in a blob made by temporaryGlslBlob(), or std::nullopt for any
/// other blob. The view points into `blob`.
[[nodiscard]] std::optional<std::string_view> temporaryGlslSource(std::span<const std::byte> blob) noexcept;

} // namespace teserra::rhi
