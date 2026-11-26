#include <teserra/debug_ui/imgui_pass.hpp>

#include <teserra/core/assert.hpp>
#include <teserra/core/log.hpp>
#include <teserra/debug_ui/debug_ui.hpp>
#include <teserra/renderer/upload_ring.hpp>
#include <teserra/rhi/binding.hpp>
#include <teserra/rhi/command_list.hpp>
#include <teserra/rhi/format.hpp>
#include <teserra/rhi/limits.hpp>
#include <teserra/rhi/pipeline.hpp>
#include <teserra/rhi/render_pass.hpp>
#include <teserra/rhi/sampler.hpp>
#include <teserra/rhi/shader.hpp>
#include <teserra/rhi/texture.hpp>

#include <teserra_debug_ui/shaders.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <optional>
#include <span>
#include <utility>

namespace teserra::debugui {

namespace {

// The shader's ImGuiFrame block, std140; the range bound is the block rounded up to 16 bytes.
struct FrameBlock {
    Vec2 scale{1.0F};
    Vec2 translate{0.0F};
    u32 decodeSrgb = 0;
    u32 padding[3]{}; // NOLINT(modernize-avoid-c-arrays): std140 padding to the bound range
};
static_assert(sizeof(FrameBlock) == 32);

constexpr rhi::VertexBufferLayout kVertexBuffer{.stride = sizeof(ImDrawVert)};
constexpr std::array<rhi::VertexAttribute, 3> kVertexAttributes{{
    {.location = 0, .format = rhi::VertexFormat::Float2, .offset = offsetof(ImDrawVert, pos)},
    {.location = 1, .format = rhi::VertexFormat::Float2, .offset = offsetof(ImDrawVert, uv)},
    {.location = 2, .format = rhi::VertexFormat::Unorm8x4, .offset = offsetof(ImDrawVert, col)},
}};
static_assert(sizeof(ImDrawIdx) == 2, "the pass binds 16-bit indices");

constexpr u64 kVertexAlignment = 16;
constexpr u64 kIndexAlignment = 4;
// Bind groups of textures not drawn for this many frames are dropped.
constexpr u64 kUnusedFrames = rhi::kMaxFramesInFlight + 2;

// `point` inside the rectangle from the origin to `limit`.
Vec2 clampToOutput(Vec2 point, Vec2 limit) noexcept {
    return {std::clamp(point.x, 0.0F, limit.x), std::clamp(point.y, 0.0F, limit.y)};
}

} // namespace

Result<std::unique_ptr<ImGuiPass>> ImGuiPass::create(rhi::Device& device) {
    // The constructor is private, so make_unique cannot reach it.
    std::unique_ptr<ImGuiPass> pass(new ImGuiPass(device)); // NOLINT(cppcoreguidelines-owning-memory)
    TESERRA_TRY_ASSIGN(pass->m_vertexShader, device.createShaderModule({.stage = rhi::ShaderStage::Vertex,
                                                                        .blob = shaders::kImguiVert,
                                                                        .debugName = "imgui vertex"}));
    TESERRA_TRY_ASSIGN(pass->m_fragmentShader, device.createShaderModule({.stage = rhi::ShaderStage::Fragment,
                                                                          .blob = shaders::kImguiFrag,
                                                                          .debugName = "imgui fragment"}));
    constexpr std::array kFrameEntries{rhi::BindGroupLayoutEntry{.binding = 0,
                                                                 .type = rhi::BindingType::UniformBuffer,
                                                                 .visibility = rhi::ShaderStage::Vertex,
                                                                 .hasDynamicOffset = true}};
    TESERRA_TRY_ASSIGN(pass->m_frameLayout,
                       device.createBindGroupLayout({.entries = kFrameEntries, .debugName = "ImGui frame"}));
    constexpr std::array kTextureEntries{rhi::BindGroupLayoutEntry{
        .binding = 0, .type = rhi::BindingType::SampledTexture, .visibility = rhi::ShaderStage::Fragment}};
    TESERRA_TRY_ASSIGN(pass->m_textureLayout, device.createBindGroupLayout({.entries = kTextureEntries,
                                                                            .debugName = "ImGui texture"}));
    const std::array groups{pass->m_frameLayout, rhi::BindGroupLayoutHandle{}, pass->m_textureLayout};
    TESERRA_TRY_ASSIGN(pass->m_pipelineLayout,
                       device.createPipelineLayout({.groups = groups, .debugName = "ImGui"}));
    TESERRA_TRY_ASSIGN(pass->m_sampler,
                       device.createSampler({.mipmapMode = rhi::MipmapMode::Nearest, .debugName = "ImGui"}));
    return pass;
}

ImGuiPass::~ImGuiPass() {
    for (const TextureGroup& entry : m_textureGroups) {
        m_device.destroy(entry.group);
    }
    for (const rhi::TextureHandle texture : m_ownedTextures) {
        m_device.destroy(texture);
    }
    for (const Pipeline& pipeline : m_pipelines) {
        m_device.destroy(pipeline.pipeline);
    }
    m_device.destroy(m_frameGroup);
    m_device.destroy(m_sampler);
    m_device.destroy(m_pipelineLayout);
    m_device.destroy(m_textureLayout);
    m_device.destroy(m_frameLayout);
    m_device.destroy(m_fragmentShader);
    m_device.destroy(m_vertexShader);
}

void ImGuiPass::declare(renderer::PassBuilder& builder) {
    builder.reads(renderer::PassResource::Output);
    builder.writes(renderer::PassResource::Output);
}

void ImGuiPass::updateTexture(ImTextureData& texture) {
    if (texture.Status == ImTextureStatus_WantCreate) {
        TESERRA_ASSERT(texture.Format == ImTextureFormat_RGBA32,
                       "ImGui asked for a texture format other than RGBA32");
        Result<rhi::TextureHandle> created = m_device.createTexture({
            .format = rhi::Format::Rgba8Unorm,
            .width = static_cast<u32>(texture.Width),
            .height = static_cast<u32>(texture.Height),
            .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst,
            .debugName = "ImGui texture",
        });
        if (!created) {
            TESERRA_LOG_ERROR(LogCategory::Renderer, "Cannot create an ImGui texture: {}", created.error());
            return; // ImGui asks again next frame
        }
        const auto bytes = static_cast<usize>(texture.Width) * static_cast<usize>(texture.Height) * 4;
        m_device.writeTexture(
            *created, {.width = static_cast<u32>(texture.Width), .height = static_cast<u32>(texture.Height)},
            std::span(static_cast<const std::byte*>(texture.GetPixels()), bytes));
        m_ownedTextures.push_back(*created);
        texture.SetTexID(textureId(*created));
        texture.SetStatus(ImTextureStatus_OK);
    } else if (texture.Status == ImTextureStatus_WantUpdates) {
        const rhi::TextureHandle handle = textureFromId(texture.TexID);
        const auto pitch = static_cast<usize>(texture.GetPitch());
        for (const ImTextureRect& rect : texture.Updates) {
            if (rect.w == 0 || rect.h == 0) {
                continue;
            }
            const usize bytes = ((static_cast<usize>(rect.h) - 1) * pitch) + (static_cast<usize>(rect.w) * 4);
            m_device.writeTexture(
                handle, {.x = rect.x, .y = rect.y, .width = rect.w, .height = rect.h},
                std::span(static_cast<const std::byte*>(texture.GetPixelsAt(rect.x, rect.y)), bytes),
                static_cast<u32>(pitch));
        }
        texture.SetStatus(ImTextureStatus_OK);
    } else if (texture.Status == ImTextureStatus_WantDestroy) {
        // The device keeps the texture alive while frames in flight still sample it.
        const rhi::TextureHandle handle = textureFromId(texture.TexID);
        forgetTexture(handle);
        std::erase(m_ownedTextures, handle);
        m_device.destroy(handle);
        texture.SetTexID(ImTextureID_Invalid);
        texture.SetStatus(ImTextureStatus_Destroyed);
    }
}

rhi::BindGroupHandle ImGuiPass::textureGroup(rhi::TextureHandle texture) {
    const auto found = std::ranges::find(m_textureGroups, texture, &TextureGroup::texture);
    if (found != m_textureGroups.end()) {
        found->lastUsedFrame = m_frame;
        return found->group;
    }
    const std::array entries{rhi::BindGroupEntry{.binding = 0, .texture = texture, .sampler = m_sampler}};
    Result<rhi::BindGroupHandle> group = m_device.createBindGroup(
        {.layout = m_textureLayout, .entries = entries, .debugName = "ImGui texture"});
    if (!group) {
        TESERRA_LOG_ERROR(LogCategory::Renderer, "Cannot bind texture {} for ImGui: {}", texture,
                          group.error());
        return {};
    }
    m_textureGroups.push_back({.texture = texture, .group = *group, .lastUsedFrame = m_frame});
    return *group;
}

void ImGuiPass::forgetTexture(rhi::TextureHandle destroyed) {
    std::erase_if(m_textureGroups, [&](const TextureGroup& entry) {
        const bool owned = std::ranges::find(m_ownedTextures, entry.texture) != m_ownedTextures.end();
        if (entry.texture != destroyed && (owned || entry.lastUsedFrame + kUnusedFrames >= m_frame)) {
            return false;
        }
        m_device.destroy(entry.group);
        return true;
    });
}

rhi::GraphicsPipelineHandle ImGuiPass::pipeline(rhi::Format format) {
    const auto found = std::ranges::find(m_pipelines, format, &Pipeline::format);
    if (found != m_pipelines.end()) {
        return found->pipeline;
    }
    Result<rhi::GraphicsPipelineHandle> created = m_device.createGraphicsPipeline({
        .vertexShader = m_vertexShader,
        .fragmentShader = m_fragmentShader,
        .layout = m_pipelineLayout,
        .vertexBuffers = std::span(&kVertexBuffer, 1),
        .vertexAttributes = kVertexAttributes,
        .colorBlend = {{.enabled = true,
                        .srcColor = rhi::BlendFactor::SrcAlpha,
                        .dstColor = rhi::BlendFactor::OneMinusSrcAlpha,
                        .srcAlpha = rhi::BlendFactor::One,
                        .dstAlpha = rhi::BlendFactor::OneMinusSrcAlpha}},
        .targets = {.colorFormats = {format}},
        .debugName = "imgui",
    });
    if (!created) {
        TESERRA_LOG_ERROR(LogCategory::Renderer, "Cannot create the ImGui pipeline: {}", created.error());
    }
    const rhi::GraphicsPipelineHandle handle = created.value_or(rhi::GraphicsPipelineHandle{});
    m_pipelines.push_back({.format = format, .pipeline = handle});
    return handle;
}

void ImGuiPass::execute(renderer::PassContext& context, rhi::CommandList& commands) {
    ++m_frame;
    m_stats = {};
    ImDrawData* data = std::exchange(m_drawData, nullptr);
    // Textures first: ImGui may ask for them even in a frame that draws nothing.
    if (data != nullptr && data->Textures != nullptr) {
        for (ImTextureData* texture : *data->Textures) {
            if (texture->Status != ImTextureStatus_OK) {
                updateTexture(*texture);
            }
        }
    }
    forgetTexture({});
    m_stats.textures = static_cast<u32>(m_ownedTextures.size());

    const renderer::FrameTarget& output = context.target(renderer::PassResource::Output);
    const Vec2 framebuffer{data != nullptr ? data->DisplaySize.x * data->FramebufferScale.x : 0.0F,
                           data != nullptr ? data->DisplaySize.y * data->FramebufferScale.y : 0.0F};
    if (data == nullptr || data->TotalVtxCount == 0 || framebuffer.x <= 0.0F || framebuffer.y <= 0.0F) {
        return;
    }
    const rhi::GraphicsPipelineHandle imguiPipeline = pipeline(output.format);
    if (imguiPipeline.isNull()) {
        return; // logged when creation failed
    }

    // The frame's geometry and the block that maps ImGui's display to clip space (+Y up).
    renderer::UploadRing& uploads = context.uploads();
    const usize vertexBytes = static_cast<usize>(data->TotalVtxCount) * sizeof(ImDrawVert);
    const usize indexBytes = static_cast<usize>(data->TotalIdxCount) * sizeof(ImDrawIdx);
    const std::optional<renderer::UploadRing::Allocation> vertices =
        uploads.allocate(vertexBytes, kVertexAlignment);
    const std::optional<renderer::UploadRing::Allocation> indices =
        uploads.allocate(indexBytes, kIndexAlignment);
    const std::optional<renderer::UploadRing::Allocation> frame =
        uploads.allocate(sizeof(FrameBlock), m_device.limits().uniformBufferOffsetAlignment);
    if (!vertices || !indices || !frame) {
        return; // allocate() asserted
    }
    usize vertexAt = 0;
    usize indexAt = 0;
    for (const ImDrawList* list : data->CmdLists) {
        const usize listVertices = static_cast<usize>(list->VtxBuffer.Size) * sizeof(ImDrawVert);
        const usize listIndices = static_cast<usize>(list->IdxBuffer.Size) * sizeof(ImDrawIdx);
        std::memcpy(vertices->memory.data() + vertexAt, list->VtxBuffer.Data, listVertices);
        std::memcpy(indices->memory.data() + indexAt, list->IdxBuffer.Data, listIndices);
        vertexAt += listVertices;
        indexAt += listIndices;
    }
    const Vec2 position{data->DisplayPos.x, data->DisplayPos.y};
    const Vec2 size{data->DisplaySize.x, data->DisplaySize.y};
    const FrameBlock block{
        .scale = {2.0F / size.x, -2.0F / size.y},
        .translate = {-1.0F - (position.x * 2.0F / size.x), 1.0F + (position.y * 2.0F / size.y)},
        .decodeSrgb = rhi::formatInfo(output.format).isSrgb ? 1U : 0U};
    std::memcpy(frame->memory.data(), &block, sizeof(block));
    if (m_frameGroup.isNull()) {
        // The ring's buffer stays the same, so one group with a dynamic offset serves every frame.
        const std::array entries{rhi::BindGroupEntry{
            .binding = 0, .buffer = {.buffer = uploads.buffer(), .size = sizeof(FrameBlock)}}};
        Result<rhi::BindGroupHandle> group = m_device.createBindGroup(
            {.layout = m_frameLayout, .entries = entries, .debugName = "ImGui frame"});
        if (!group) {
            TESERRA_LOG_ERROR(LogCategory::Renderer, "Cannot create the ImGui frame group: {}",
                              group.error());
            return;
        }
        m_frameGroup = *group;
    }

    commands.beginRenderPass(
        {.colorAttachments = {
             {.texture = output.texture, .loadOp = rhi::LoadOp::Load, .storeOp = rhi::StoreOp::Store}}});
    const std::array offsets{static_cast<u32>(frame->offset)};
    const auto bindState = [&] {
        commands.setViewport({.x = 0.0F, .y = 0.0F, .width = framebuffer.x, .height = framebuffer.y});
        commands.bindPipeline(imguiPipeline);
        commands.bindGroup(rhi::kFrameGroup, m_frameGroup, offsets);
        commands.bindVertexBuffer(0, vertices->buffer, vertices->offset);
        commands.bindIndexBuffer(indices->buffer, rhi::IndexType::Uint16, indices->offset);
    };
    bindState();
    const Vec2 scale{data->FramebufferScale.x, data->FramebufferScale.y};
    const Vec2 limit{static_cast<f32>(output.size.x), static_cast<f32>(output.size.y)};
    u32 listVertexOffset = 0;
    u32 listIndexOffset = 0;
    for (ImDrawList* list : data->CmdLists) {
        for (const ImDrawCmd& command : list->CmdBuffer) {
            if (command.UserCallback != nullptr) {
                if (command.UserCallback == ImDrawCallback_ResetRenderState) {
                    bindState();
                } else {
                    command.UserCallback(list, &command);
                }
                continue;
            }
            // The clip rectangle in framebuffer pixels, inside the output.
            const Vec2 min =
                clampToOutput((Vec2{command.ClipRect.x, command.ClipRect.y} - position) * scale, limit);
            const Vec2 max =
                clampToOutput((Vec2{command.ClipRect.z, command.ClipRect.w} - position) * scale, limit);
            if (max.x <= min.x || max.y <= min.y || command.ElemCount == 0) {
                continue;
            }
            const rhi::BindGroupHandle texture = textureGroup(textureFromId(command.GetTexID()));
            if (texture.isNull()) {
                continue;
            }
            commands.setScissor({.x = static_cast<u32>(min.x),
                                 .y = static_cast<u32>(min.y),
                                 .width = static_cast<u32>(max.x - min.x),
                                 .height = static_cast<u32>(max.y - min.y)});
            commands.bindGroup(rhi::kMaterialGroup, texture);
            commands.drawIndexed({.indexCount = command.ElemCount,
                                  .firstIndex = listIndexOffset + command.IdxOffset,
                                  .vertexOffset = static_cast<i32>(listVertexOffset + command.VtxOffset)});
            ++m_stats.drawCalls;
        }
        listVertexOffset += static_cast<u32>(list->VtxBuffer.Size);
        listIndexOffset += static_cast<u32>(list->IdxBuffer.Size);
    }
    commands.endRenderPass();
    m_stats.vertices = static_cast<u32>(data->TotalVtxCount);
    m_stats.indices = static_cast<u32>(data->TotalIdxCount);
}

} // namespace teserra::debugui
