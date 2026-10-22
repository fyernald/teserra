// RHI triangle: the first geometry through the RHI (#35), and the hardware-check build of iteration 5. An
// indexed, vertex-colored triangle is drawn every frame from RHI buffers, shader modules and a graphics
// pipeline. Only RHI calls: no backend appears here.
//
//   teserra_rhi_triangle                               runs until Escape or close
//   teserra_rhi_triangle --frames 120 --hidden-window  smoke run: 120 frames in a hidden window
//
// Everything that is logged also goes to teserra.log in the working directory, for hardware-check reports.

#include <teserra/core/log.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/engine/app.hpp>
#include <teserra/platform/key.hpp>
#include <teserra/rhi/command_list.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/pipeline.hpp>
#include <teserra/rhi/render_pass.hpp>
#include <teserra/rhi/shader.hpp>

#include <array>
#include <span>
#include <string_view>

using namespace teserra;

namespace {

// TEMPORARY (#44): GLSL until teserra_shaderc compiles shader files into .tshader blobs.
constexpr std::string_view kVertexShader = R"(#version 450 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 vColor;

void main() {
    vColor = color;
    gl_Position = vec4(position, 1.0);
}
)";

constexpr std::string_view kFragmentShader = R"(#version 450 core
layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vColor;
}
)";

struct Vertex {
    f32 position[3]; // NOLINT(modernize-avoid-c-arrays): plain vertex data, laid out as the GPU reads it
    f32 color[4];    // NOLINT(modernize-avoid-c-arrays)
};

// Counter-clockwise in normalized device coordinates (+Y up), the RHI's front faces (ADR-0024): red at the
// top, green bottom-left, blue bottom-right.
constexpr std::array<Vertex, 3> kVertices{{
    {.position = {0.0F, 0.5F, 0.0F}, .color = {1.0F, 0.0F, 0.0F, 1.0F}},
    {.position = {-0.5F, -0.5F, 0.0F}, .color = {0.0F, 1.0F, 0.0F, 1.0F}},
    {.position = {0.5F, -0.5F, 0.0F}, .color = {0.0F, 0.0F, 1.0F, 1.0F}},
}};
constexpr std::array<u16, 3> kIndices{0, 1, 2};

// The GPU objects of the triangle, created on the App's device and destroyed before it.
class Triangle {
public:
    explicit Triangle(rhi::Device& device)
        : m_device(device) {}
    ~Triangle() {
        m_device.destroy(m_pipeline);
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
                                               .blob = rhi::temporaryGlslBlob(kVertexShader),
                                               .debugName = "Triangle vertex",
                                           }));
        TESERRA_TRY_ASSIGN(m_fragmentShader, m_device.createShaderModule({
                                                 .stage = rhi::ShaderStage::Fragment,
                                                 .blob = rhi::temporaryGlslBlob(kFragmentShader),
                                                 .debugName = "Triangle fragment",
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
                                           .vertexBuffers = kBuffers,
                                           .vertexAttributes = kAttributes,
                                           // Back faces are culled: a wrong winding would hide the triangle.
                                           .raster = {.cullMode = rhi::CullMode::Back},
                                           .targets = {.colorFormats = {targetFormat}},
                                           .debugName = "Triangle",
                                       }));
        return {};
    }

    void record(rhi::CommandList& commands, rhi::TextureHandle target) const {
        commands.pushDebugGroup("Triangle");
        commands.beginRenderPass({.colorAttachments = {{
                                      .texture = target,
                                      .loadOp = rhi::LoadOp::Clear,
                                      .storeOp = rhi::StoreOp::Store,
                                      .clearColor = {0.05F, 0.05F, 0.06F, 1.0F},
                                  }}});
        commands.bindPipeline(m_pipeline);
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
    rhi::GraphicsPipelineHandle m_pipeline;
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
    TESERRA_LOG_INFO(LogCategory::App, "Drawing a triangle with the {} backend; press Escape to quit",
                     rhi::backendName(app.device().backend()));

    const int exitCode = app.run({
        .update =
            [](engine::App& running, f32 /*deltaSeconds*/) {
                if (running.input().wasPressed(platform::Key::Escape)) {
                    running.quit();
                }
            },
        .render = [&](engine::App& running,
                      rhi::CommandList& commands) { triangle.record(commands, running.backbuffer()); },
    });
    log::flush();
    return exitCode;
}
