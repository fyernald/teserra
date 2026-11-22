// Textured quad: the headline of iteration 7 (#50). A PNG is read with the platform's FileSystem, decoded
// with assets::decodeImage(), uploaded to an RHI texture and drawn on a quad through a bind group that pairs
// the texture with a sampler. Space switches between two bind groups, one with a nearest and one with a
// linear sampler; the window title shows which. Only RHI calls: no backend appears here.
//
// The image (assets/orientation_f.png, 16x16) has a red marker in its top-left corner and an upright "F", so
// a flipped or mirrored upload shows at a glance. Magnified, nearest filtering keeps its pixels sharp and
// linear filtering blends them.
//
//   teserra_textured_quad                               runs until Escape or close
//   teserra_textured_quad --frames 120 --hidden-window  smoke run: 120 frames in a hidden window

#include <teserra/assets/image.hpp>
#include <teserra/core/log.hpp>
#include <teserra/core/math.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/engine/app.hpp>
#include <teserra/platform/file_system.hpp>
#include <teserra/platform/key.hpp>
#include <teserra/platform/platform.hpp>
#include <teserra/platform/window.hpp>
#include <teserra/rhi/binding.hpp>
#include <teserra/rhi/command_list.hpp>
#include <teserra/rhi/commands.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/pipeline.hpp>
#include <teserra/rhi/render_pass.hpp>
#include <teserra/rhi/sampler.hpp>
#include <teserra/rhi/shader.hpp>
#include <teserra/rhi/texture.hpp>

#include <textured_quad/shaders.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace teserra;

namespace {

// Relative to the executable's directory; CMake copies the image there.
constexpr std::string_view kImagePath = "assets/orientation_f.png";

struct Vertex {
    Vec2 position;
    Vec2 uv;
};

// A square over the whole viewport, counter-clockwise in normalized device coordinates (+Y up), the RHI's
// front faces (ADR-0024). UV (0, 0) is the image's top-left texel, so the top-left corner gets it.
constexpr std::array<Vertex, 4> kVertices{{
    {.position = {-1.0F, 1.0F}, .uv = {0.0F, 0.0F}},  // top-left
    {.position = {-1.0F, -1.0F}, .uv = {0.0F, 1.0F}}, // bottom-left
    {.position = {1.0F, -1.0F}, .uv = {1.0F, 1.0F}},  // bottom-right
    {.position = {1.0F, 1.0F}, .uv = {1.0F, 0.0F}},   // top-right
}};
constexpr std::array<u16, 6> kIndices{0, 1, 2, 0, 2, 3};

// The share of the window's shorter side that the quad covers.
constexpr f32 kQuadScale = 0.8F;

// Reads and decodes the image next to the executable.
Result<assets::ImageData> loadImage(platform::FileSystem& files) {
    TESERRA_TRY_ASSIGN(const std::string directory, files.executableDirectory());
    TESERRA_TRY_ASSIGN(const std::vector<std::byte> bytes,
                       files.readFile(directory + "/" + std::string(kImagePath)));
    return assets::decodeImage(bytes);
}

// The GPU objects of the quad, created on the App's device and destroyed before it.
class TexturedQuad {
public:
    explicit TexturedQuad(rhi::Device& device)
        : m_device(device) {}
    ~TexturedQuad() {
        m_device.destroy(m_pipeline);
        m_device.destroy(m_linearGroup);
        m_device.destroy(m_nearestGroup);
        m_device.destroy(m_linearSampler);
        m_device.destroy(m_nearestSampler);
        m_device.destroy(m_texture);
        m_device.destroy(m_pipelineLayout);
        m_device.destroy(m_materialLayout);
        m_device.destroy(m_fragmentShader);
        m_device.destroy(m_vertexShader);
        m_device.destroy(m_indices);
        m_device.destroy(m_vertices);
    }

    TexturedQuad(const TexturedQuad&) = delete;
    TexturedQuad& operator=(const TexturedQuad&) = delete;
    TexturedQuad(TexturedQuad&&) = delete;
    TexturedQuad& operator=(TexturedQuad&&) = delete;

    [[nodiscard]] Result<void> create(const assets::ImageData& image, rhi::Format targetFormat) {
        TESERRA_TRY_ASSIGN(m_vertices, m_device.createBuffer({
                                           .sizeInBytes = sizeof(kVertices),
                                           .usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst,
                                           .debugName = "Quad vertices",
                                       }));
        TESERRA_TRY_ASSIGN(m_indices, m_device.createBuffer({
                                          .sizeInBytes = sizeof(kIndices),
                                          .usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst,
                                          .debugName = "Quad indices",
                                      }));
        m_device.writeBuffer(m_vertices, 0, std::as_bytes(std::span(kVertices)));
        m_device.writeBuffer(m_indices, 0, std::as_bytes(std::span(kIndices)));

        // The image holds colors, so it is stored as sRGB and read back in linear values by the shader.
        TESERRA_TRY_ASSIGN(m_texture, m_device.createTexture({
                                          .format = rhi::Format::Rgba8Srgb,
                                          .width = image.width,
                                          .height = image.height,
                                          .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst,
                                          .debugName = kImagePath,
                                      }));
        m_device.writeTexture(m_texture, {.width = image.width, .height = image.height}, image.pixels);

        TESERRA_TRY_ASSIGN(m_nearestSampler, m_device.createSampler({
                                                 .minFilter = rhi::Filter::Nearest,
                                                 .magFilter = rhi::Filter::Nearest,
                                                 .mipmapMode = rhi::MipmapMode::Nearest,
                                                 .debugName = "Nearest",
                                             }));
        TESERRA_TRY_ASSIGN(m_linearSampler, m_device.createSampler({.debugName = "Linear"}));

        TESERRA_TRY_ASSIGN(m_vertexShader, m_device.createShaderModule({
                                               .stage = rhi::ShaderStage::Vertex,
                                               .blob = examples::shaders::kQuadVert,
                                               .debugName = "Quad vertex",
                                           }));
        TESERRA_TRY_ASSIGN(m_fragmentShader, m_device.createShaderModule({
                                                 .stage = rhi::ShaderStage::Fragment,
                                                 .blob = examples::shaders::kQuadFrag,
                                                 .debugName = "Quad fragment",
                                             }));

        // The texture is a material resource, at set 2 (kMaterialGroup); sets 0 and 1 stay empty.
        constexpr std::array kMaterialEntries{rhi::BindGroupLayoutEntry{
            .binding = 0,
            .type = rhi::BindingType::SampledTexture,
            .visibility = rhi::ShaderStage::Fragment,
        }};
        TESERRA_TRY_ASSIGN(m_materialLayout, m_device.createBindGroupLayout(
                                                 {.entries = kMaterialEntries, .debugName = "Material"}));
        const std::array groups{rhi::BindGroupLayoutHandle{}, rhi::BindGroupLayoutHandle{}, m_materialLayout};
        TESERRA_TRY_ASSIGN(m_pipelineLayout,
                           m_device.createPipelineLayout({.groups = groups, .debugName = "Textured quad"}));
        // Bind groups are immutable: one per sampler, and switching filters binds the other one.
        const std::array nearestResources{
            rhi::BindGroupEntry{.binding = 0, .texture = m_texture, .sampler = m_nearestSampler}};
        TESERRA_TRY_ASSIGN(m_nearestGroup, m_device.createBindGroup({
                                               .layout = m_materialLayout,
                                               .entries = nearestResources,
                                               .debugName = "Quad nearest",
                                           }));
        const std::array linearResources{
            rhi::BindGroupEntry{.binding = 0, .texture = m_texture, .sampler = m_linearSampler}};
        TESERRA_TRY_ASSIGN(m_linearGroup, m_device.createBindGroup({
                                              .layout = m_materialLayout,
                                              .entries = linearResources,
                                              .debugName = "Quad linear",
                                          }));

        constexpr std::array kBuffers{rhi::VertexBufferLayout{.stride = sizeof(Vertex)}};
        constexpr std::array kAttributes{
            rhi::VertexAttribute{.location = 0, .format = rhi::VertexFormat::Float2, .offset = 0},
            rhi::VertexAttribute{.location = 1, .format = rhi::VertexFormat::Float2, .offset = sizeof(Vec2)},
        };
        TESERRA_TRY_ASSIGN(m_pipeline, m_device.createGraphicsPipeline({
                                           .vertexShader = m_vertexShader,
                                           .fragmentShader = m_fragmentShader,
                                           .layout = m_pipelineLayout,
                                           .vertexBuffers = kBuffers,
                                           .vertexAttributes = kAttributes,
                                           // Back faces are culled: a wrong winding would hide the quad.
                                           .raster = {.cullMode = rhi::CullMode::Back},
                                           .targets = {.colorFormats = {targetFormat}},
                                           .debugName = "Textured quad",
                                       }));
        return {};
    }

    void toggleFilter() { m_linear = !m_linear; }
    [[nodiscard]] std::string_view filterName() const noexcept { return m_linear ? "linear" : "nearest"; }

    void record(rhi::CommandList& commands, rhi::TextureHandle target, UVec2 targetSize) {
        commands.pushDebugGroup("Textured quad");
        commands.beginRenderPass({.colorAttachments = {{
                                      .texture = target,
                                      .loadOp = rhi::LoadOp::Clear,
                                      .storeOp = rhi::StoreOp::Store,
                                      .clearColor = {0.05F, 0.05F, 0.06F, 1.0F},
                                  }}});
        // A centered square viewport keeps the image's pixels square in any window shape.
        const f32 side = static_cast<f32>(std::min(targetSize.x, targetSize.y)) * kQuadScale;
        commands.setViewport({
            .x = (static_cast<f32>(targetSize.x) - side) * 0.5F,
            .y = (static_cast<f32>(targetSize.y) - side) * 0.5F,
            .width = side,
            .height = side,
        });
        commands.bindPipeline(m_pipeline);
        commands.bindGroup(rhi::kMaterialGroup, m_linear ? m_linearGroup : m_nearestGroup);
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
    rhi::TextureHandle m_texture;
    rhi::SamplerHandle m_nearestSampler;
    rhi::SamplerHandle m_linearSampler;
    rhi::ShaderModuleHandle m_vertexShader;
    rhi::ShaderModuleHandle m_fragmentShader;
    rhi::BindGroupLayoutHandle m_materialLayout;
    rhi::PipelineLayoutHandle m_pipelineLayout;
    rhi::BindGroupHandle m_nearestGroup;
    rhi::BindGroupHandle m_linearGroup;
    rhi::GraphicsPipelineHandle m_pipeline;
    bool m_linear = false; // starts with nearest: the image's pixels as drawn
};

void showFilter(engine::App& app, const TexturedQuad& quad) {
    app.window().setTitle(std::format("Teserra: textured quad ({}, Space to switch)", quad.filterName()));
}

} // namespace

int main(int argc, char** argv) { // NOLINT(bugprone-exception-escape)
    engine::App app{{.title = "Teserra: textured quad", .args = {argc, argv}}};
    if (!app.isInitialized()) {
        log::flush();
        return 1;
    }
    const Result<assets::ImageData> image = loadImage(app.platform().fileSystem());
    if (!image) {
        TESERRA_LOG_ERROR(LogCategory::App, "Could not load {}: {}", kImagePath, image.error());
        log::flush();
        return 1;
    }
    TexturedQuad quad(app.device());
    if (const Result<void> created = quad.create(*image, app.device().swapchain()->format()); !created) {
        TESERRA_LOG_ERROR(LogCategory::App, "Could not create the quad: {}", created.error());
        log::flush();
        return 1;
    }
    TESERRA_LOG_INFO(LogCategory::App,
                     "Drawing {} ({}x{}) with the {} backend; Space switches filtering, Escape quits",
                     kImagePath, image->width, image->height, rhi::backendName(app.device().backend()));
    showFilter(app, quad);

    const int exitCode = app.run({
        .update =
            [&](engine::App& running, f32 /*deltaSeconds*/) {
                if (running.input().wasPressed(platform::Key::Escape)) {
                    running.quit();
                }
                if (running.input().wasPressed(platform::Key::Space)) {
                    quad.toggleFilter();
                    showFilter(running, quad);
                    TESERRA_LOG_INFO(LogCategory::App, "Filtering: {}", quad.filterName());
                }
            },
        .render =
            [&](engine::App& running, rhi::CommandList& commands) {
                quad.record(commands, running.backbuffer(), running.device().swapchain()->size());
            },
    });
    log::flush();
    return exitCode;
}
