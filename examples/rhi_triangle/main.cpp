// RHI triangle: the first geometry through the RHI (#35), and the hardware-check build of iteration 5. An
// indexed, vertex-colored triangle is drawn every frame from RHI buffers, shader modules and a graphics
// pipeline, and turns by an angle from a per-frame uniform buffer at set 0 (#43). Only RHI calls: no backend
// appears here.
//
//   teserra_rhi_triangle                               runs until Escape or close
//   teserra_rhi_triangle --frames 120 --hidden-window  smoke run: 120 frames in a hidden window
//
// Everything that is logged also goes to teserra.log in the working directory, for hardware-check reports.

#include <teserra/core/log.hpp>
#include <teserra/core/math.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/engine/app.hpp>
#include <teserra/platform/key.hpp>
#include <teserra/rhi/binding.hpp>
#include <teserra/rhi/command_list.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/limits.hpp>
#include <teserra/rhi/pipeline.hpp>
#include <teserra/rhi/render_pass.hpp>
#include <teserra/rhi/shader.hpp>

#include <rhi_triangle/shaders.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>
#include <span>

using namespace teserra;

namespace {

struct Vertex {
    f32 position[3]; // NOLINT(modernize-avoid-c-arrays): plain vertex data, laid out as the GPU reads it
    f32 color[4];    // NOLINT(modernize-avoid-c-arrays)
};

// Counter-clockwise in normalized device coordinates (+Y up), the RHI's front faces (ADR-0024), and centered
// on the origin, which it turns around: red at the top, green bottom-left, blue bottom-right.
constexpr std::array<Vertex, 3> kVertices{{
    {.position = {0.0F, 0.6F, 0.0F}, .color = {1.0F, 0.0F, 0.0F, 1.0F}},
    {.position = {-0.52F, -0.3F, 0.0F}, .color = {0.0F, 1.0F, 0.0F, 1.0F}},
    {.position = {0.52F, -0.3F, 0.0F}, .color = {0.0F, 0.0F, 1.0F, 1.0F}},
}};
constexpr std::array<u16, 3> kIndices{0, 1, 2};

// The shader's Frame block (std140: two vec2s).
struct FrameUniforms {
    Vec2 rotation; // cosine and sine of the angle
    Vec2 scale;    // keeps the triangle's shape in windows that are not square
};
static_assert(sizeof(FrameUniforms) == 16);

constexpr f32 kTurnsPerSecond = 0.125F;

// The GPU objects of the triangle, created on the App's device and destroyed before it.
class Triangle {
public:
    explicit Triangle(rhi::Device& device)
        : m_device(device) {}
    ~Triangle() {
        m_device.destroy(m_pipeline);
        m_device.destroy(m_frameGroup);
        m_device.destroy(m_frameUniforms);
        m_device.destroy(m_pipelineLayout);
        m_device.destroy(m_frameLayout);
        m_device.destroy(m_fragmentShader);
        m_device.destroy(m_vertexShader);
        m_device.destroy(m_indices);
        m_device.destroy(m_vertices);
    }

    Triangle(const Triangle&) = delete;
    Triangle& operator=(const Triangle&) = delete;
    Triangle(Triangle&&) = delete;
    Triangle& operator=(Triangle&&) = delete;

    [[nodiscard]] Result<void> create(rhi::Format targetFormat) {
        TESERRA_TRY_ASSIGN(m_vertices, m_device.createBuffer({
                                           .sizeInBytes = sizeof(kVertices),
                                           .usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst,
                                           .debugName = "Triangle vertices",
                                       }));
        TESERRA_TRY_ASSIGN(m_indices, m_device.createBuffer({
                                          .sizeInBytes = sizeof(kIndices),
                                          .usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst,
                                          .debugName = "Triangle indices",
                                      }));
        m_device.writeBuffer(m_vertices, 0, std::as_bytes(std::span(kVertices)));
        m_device.writeBuffer(m_indices, 0, std::as_bytes(std::span(kIndices)));

        TESERRA_TRY_ASSIGN(m_vertexShader, m_device.createShaderModule({
                                               .stage = rhi::ShaderStage::Vertex,
                                               .blob = examples::shaders::kTriangleVert,
                                               .debugName = "Triangle vertex",
                                           }));
        TESERRA_TRY_ASSIGN(m_fragmentShader, m_device.createShaderModule({
                                                 .stage = rhi::ShaderStage::Fragment,
                                                 .blob = examples::shaders::kTriangleFrag,
                                                 .debugName = "Triangle fragment",
                                             }));

        // The frame's uniforms: one slot per frame in flight, each at the device's offset alignment. A frame
        // writes its slot and binds the group with the slot's dynamic offset, so it never overwrites what
        // the frame before may still read.
        m_slotStride = std::max<u32>(sizeof(FrameUniforms), m_device.limits().uniformBufferOffsetAlignment);
        TESERRA_TRY_ASSIGN(m_frameUniforms, m_device.createBuffer({
                                                .sizeInBytes = u64{m_slotStride} * rhi::kMaxFramesInFlight,
                                                .usage = rhi::BufferUsage::Uniform,
                                                .memory = rhi::MemoryLocation::Upload,
                                                .debugName = "Triangle frame uniforms",
                                            }));
        constexpr std::array kFrameEntries{rhi::BindGroupLayoutEntry{
            .binding = 0,
            .type = rhi::BindingType::UniformBuffer,
            .visibility = rhi::ShaderStage::Vertex,
            .hasDynamicOffset = true,
        }};
        TESERRA_TRY_ASSIGN(m_frameLayout,
                           m_device.createBindGroupLayout({.entries = kFrameEntries, .debugName = "Frame"}));
        const std::array groups{m_frameLayout};
        TESERRA_TRY_ASSIGN(m_pipelineLayout,
                           m_device.createPipelineLayout({.groups = groups, .debugName = "Triangle"}));
        const std::array frameResources{rhi::BindGroupEntry{
            .binding = 0,
            .buffer = {.buffer = m_frameUniforms, .size = sizeof(FrameUniforms)},
        }};
        TESERRA_TRY_ASSIGN(m_frameGroup, m_device.createBindGroup({
                                             .layout = m_frameLayout,
                                             .entries = frameResources,
                                             .debugName = "Triangle frame",
                                         }));

        constexpr std::array kBuffers{rhi::VertexBufferLayout{.stride = sizeof(Vertex)}};
        constexpr std::array kAttributes{
            rhi::VertexAttribute{.location = 0, .format = rhi::VertexFormat::Float3, .offset = 0},
            rhi::VertexAttribute{
                .location = 1, .format = rhi::VertexFormat::Float4, .offset = sizeof(f32) * 3},
        };
        TESERRA_TRY_ASSIGN(m_pipeline, m_device.createGraphicsPipeline({
                                           .vertexShader = m_vertexShader,
                                           .fragmentShader = m_fragmentShader,
                                           .layout = m_pipelineLayout,
                                           .vertexBuffers = kBuffers,
                                           .vertexAttributes = kAttributes,
                                           // Back faces are culled: a wrong winding would hide the triangle.
                                           .raster = {.cullMode = rhi::CullMode::Back},
                                           .targets = {.colorFormats = {targetFormat}},
                                           .debugName = "Triangle",
                                       }));
        return {};
    }

    void update(f32 deltaSeconds) { m_turns = std::fmod(m_turns + (deltaSeconds * kTurnsPerSecond), 1.0F); }

    void record(rhi::CommandList& commands, rhi::TextureHandle target, UVec2 targetSize) {
        const f32 angle = m_turns * 2.0F * std::numbers::pi_v<f32>;
        FrameUniforms uniforms{.rotation = {std::cos(angle), std::sin(angle)}, .scale = {1.0F, 1.0F}};
        if (targetSize.x > 0 && targetSize.y > 0) {
            const auto shorter = static_cast<f32>(std::min(targetSize.x, targetSize.y));
            uniforms.scale = {shorter / static_cast<f32>(targetSize.x),
                              shorter / static_cast<f32>(targetSize.y)};
        }
        const u32 offset = m_slotStride * static_cast<u32>(m_frame++ % rhi::kMaxFramesInFlight);
        std::memcpy(m_device.map(m_frameUniforms).subspan(offset).data(), &uniforms, sizeof(uniforms));

        commands.pushDebugGroup("Triangle");
        commands.beginRenderPass({.colorAttachments = {{
                                      .texture = target,
                                      .loadOp = rhi::LoadOp::Clear,
                                      .storeOp = rhi::StoreOp::Store,
                                      .clearColor = {0.05F, 0.05F, 0.06F, 1.0F},
                                  }}});
        commands.bindPipeline(m_pipeline);
        const std::array offsets{offset};
        commands.bindGroup(rhi::kFrameGroup, m_frameGroup, offsets);
        commands.bindVertexBuffer(0, m_vertices);
        commands.bindIndexBuffer(m_indices, rhi::IndexType::Uint16);
        commands.drawIndexed({.indexCount = static_cast<u32>(kIndices.size())});
        commands.endRenderPass();
        commands.popDebugGroup();
    }

private:
    rhi::Device& m_device;
    rhi::BufferHandle m_vertices;
    rhi::BufferHandle m_indices;
    rhi::ShaderModuleHandle m_vertexShader;
    rhi::ShaderModuleHandle m_fragmentShader;
    rhi::BufferHandle m_frameUniforms;
    rhi::BindGroupLayoutHandle m_frameLayout;
    rhi::PipelineLayoutHandle m_pipelineLayout;
    rhi::BindGroupHandle m_frameGroup;
    rhi::GraphicsPipelineHandle m_pipeline;
    u32 m_slotStride = 0;
    u64 m_frame = 0;
    f32 m_turns = 0.0F; // of the full circle, 0 to 1
};

} // namespace

int main(int argc, char** argv) { // NOLINT(bugprone-exception-escape)
    if (const Result<void> logged = log::configure({.filePath = "teserra.log"}); !logged) {
        TESERRA_LOG_WARN(LogCategory::App, "No log file: {}", logged.error());
    }

    engine::App app{{.title = "Teserra: RHI triangle", .args = {argc, argv}}};
    if (!app.isInitialized()) {
        log::flush();
        return 1;
    }
    Triangle triangle(app.device());
    if (const Result<void> created = triangle.create(app.device().swapchain()->format()); !created) {
        TESERRA_LOG_ERROR(LogCategory::App, "Could not create the triangle: {}", created.error());
        log::flush();
        return 1;
    }
    TESERRA_LOG_INFO(LogCategory::App, "Turning a triangle with the {} backend; press Escape to quit",
                     rhi::backendName(app.device().backend()));

    const int exitCode = app.run({
        .update =
            [&](engine::App& running, f32 deltaSeconds) {
                if (running.input().wasPressed(platform::Key::Escape)) {
                    running.quit();
                }
                triangle.update(deltaSeconds);
            },
        .render =
            [&](engine::App& running, rhi::CommandList& commands) {
                triangle.record(commands, running.backbuffer(), running.device().swapchain()->size());
            },
    });
    log::flush();
    return exitCode;
}
