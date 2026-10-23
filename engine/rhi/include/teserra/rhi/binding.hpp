#pragma once

// Resource bindings: shaders declare resources at (group, binding), `layout(set = S, binding = B)` in GLSL
// (docs/architecture/rhi.md#binding-model).

#include <teserra/core/types.hpp>

namespace teserra::rhi {

/// What a binding holds. These are the v1 types; textures are always combined with a sampler.
enum class BindingType : u8 {
    UniformBuffer,
    StorageBuffer, ///< read-only in v1
    SampledTexture,
};

} // namespace teserra::rhi
