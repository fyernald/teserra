#pragma once

// Handles of RHI objects (docs/architecture/rhi.md#handles-and-resource-lifetime). The device that created
// an object owns it; a handle is a non-owning reference that becomes stale when the object is destroyed.
// The default value is the null handle.

#include <teserra/core/handle.hpp>

namespace teserra::rhi {

using BufferHandle = Handle<struct BufferTag>;
using TextureHandle = Handle<struct TextureTag>;
using SamplerHandle = Handle<struct SamplerTag>;
using ShaderModuleHandle = Handle<struct ShaderModuleTag>;
/// Shape of a group of shader resources (iteration 6).
using BindGroupLayoutHandle = Handle<struct BindGroupLayoutTag>;
/// Ordered bind group layouts of a pipeline (iteration 6).
using PipelineLayoutHandle = Handle<struct PipelineLayoutTag>;
/// Concrete resources matching a bind group layout (iteration 6).
using BindGroupHandle = Handle<struct BindGroupTag>;
using GraphicsPipelineHandle = Handle<struct GraphicsPipelineTag>;

} // namespace teserra::rhi
