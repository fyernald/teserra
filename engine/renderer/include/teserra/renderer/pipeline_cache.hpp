#pragma once

// The pipeline cache (docs/architecture/renderer.md#materials-shaders-and-uniforms): pipelines are created
// when first needed and kept, keyed by the shaders and layout, the material's state, the vertex layout and
// the target layout. Creation is logged at Debug and never happens on a per-draw path.

#include <teserra/core/types.hpp>
#include <teserra/renderer/material.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/handles.hpp>
#include <teserra/rhi/pipeline.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace teserra::renderer {

/// A vertex layout as RHI data. Layouts are compared by the address of their attributes, so each one is a
/// constant such as kSpriteVertexAttributes.
struct VertexLayoutRef {
    std::span<const rhi::VertexBufferLayout> buffers{};
    std::span<const rhi::VertexAttribute> attributes{};
};

/// What a pipeline is made of.
struct PipelineKey {
    rhi::ShaderModuleHandle vertexShader{};
    rhi::ShaderModuleHandle fragmentShader{};
    rhi::PipelineLayoutHandle layout{};
    MaterialState state{};
    VertexLayoutRef vertexLayout{};
    rhi::Format colorFormat = rhi::Format::Undefined;
    rhi::Format depthFormat = rhi::Format::Undefined;
};

/// Creates pipelines on first use and keeps them until it is destroyed or they are forgotten. Lookups go
/// through a small array: a steady-state frame allocates nothing.
class PipelineCache {
public:
    explicit PipelineCache(rhi::Device& device) noexcept
        : m_device(device) {}
    ~PipelineCache();

    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;
    PipelineCache(PipelineCache&&) = delete;
    PipelineCache& operator=(PipelineCache&&) = delete;

    /// The pipeline of `key`, created now if needed (logged at Debug with `debugName`); null if creation
    /// failed, which is logged once and remembered.
    [[nodiscard]] rhi::GraphicsPipelineHandle get(const PipelineKey& key, std::string_view debugName);
    /// Destroys the pipelines that use `shader`, e.g. before a material's shaders unload.
    void forget(rhi::ShaderModuleHandle shader);

    [[nodiscard]] usize size() const noexcept { return m_entries.size(); }

private:
    struct Entry {
        PipelineKey key;
        rhi::GraphicsPipelineHandle pipeline;
    };

    rhi::Device& m_device;
    std::vector<Entry> m_entries;
};

} // namespace teserra::renderer
