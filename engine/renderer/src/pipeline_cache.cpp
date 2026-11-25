#include <teserra/renderer/pipeline_cache.hpp>

#include <teserra/core/log.hpp>

#include <algorithm>

namespace teserra::renderer {

namespace {

bool sameKey(const PipelineKey& a, const PipelineKey& b) noexcept {
    return a.vertexShader == b.vertexShader && a.fragmentShader == b.fragmentShader && a.layout == b.layout &&
           a.state == b.state && a.vertexLayout.attributes.data() == b.vertexLayout.attributes.data() &&
           a.vertexLayout.buffers.data() == b.vertexLayout.buffers.data() && a.colorFormat == b.colorFormat &&
           a.depthFormat == b.depthFormat;
}

} // namespace

PipelineCache::~PipelineCache() {
    for (const Entry& entry : m_entries) {
        m_device.destroy(entry.pipeline);
    }
}

rhi::GraphicsPipelineHandle PipelineCache::get(const PipelineKey& key, std::string_view debugName) {
    const auto found =
        std::ranges::find_if(m_entries, [&](const Entry& entry) { return sameKey(entry.key, key); });
    if (found != m_entries.end()) {
        return found->pipeline;
    }
    Result<rhi::GraphicsPipelineHandle> created = m_device.createGraphicsPipeline({
        .vertexShader = key.vertexShader,
        .fragmentShader = key.fragmentShader,
        .layout = key.layout,
        .vertexBuffers = key.vertexLayout.buffers,
        .vertexAttributes = key.vertexLayout.attributes,
        .raster = {.cullMode = key.state.cullMode},
        .depthStencil = {.depthTest = key.state.depthTest,
                         .depthWrite = key.state.depthWrite,
                         .depthCompare = rhi::CompareOp::LessOrEqual},
        .colorBlend = {blendState(key.state.blend)},
        .targets = {.colorFormats = {key.colorFormat}, .depthStencilFormat = key.depthFormat},
        .debugName = debugName,
    });
    if (!created) {
        TESERRA_LOG_ERROR(LogCategory::Renderer, "Could not create the pipeline of '{}': {}", debugName,
                          created.error());
    } else {
        // Pipelines are created during warm-up, never per draw; the log shows when.
        TESERRA_LOG_DEBUG(LogCategory::Renderer, "Created the pipeline of '{}'", debugName);
    }
    const rhi::GraphicsPipelineHandle handle = created.value_or(rhi::GraphicsPipelineHandle{});
    m_entries.push_back({.key = key, .pipeline = handle});
    return handle;
}

void PipelineCache::forget(rhi::ShaderModuleHandle shader) {
    std::erase_if(m_entries, [&](const Entry& entry) {
        if (entry.key.vertexShader != shader && entry.key.fragmentShader != shader) {
            return false;
        }
        m_device.destroy(entry.pipeline);
        return true;
    });
}

} // namespace teserra::renderer
