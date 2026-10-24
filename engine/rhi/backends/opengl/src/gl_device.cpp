#include "gl_device.hpp"

#include <teserra/core/assert.hpp>
#include <teserra/core/log.hpp>
#include <teserra/platform/opengl_context.hpp>
#include <teserra/platform/window.hpp>
#include <teserra/rhi/shader_blob.hpp>
#include <teserra/rhi/validation.hpp>

#include "gl_debug.hpp"
#include "gl_format.hpp"
#include "gl_swapchain.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace teserra::rhi::opengl {

namespace {

GLADapiproc loadFunction(void* context, const char* name) {
    return static_cast<platform::OpenGlContext*>(context)->getProcAddress(name);
}

// The device's objects for validateCommands().
class GlLookup final : public ResourceLookup {
public:
    explicit GlLookup(const GlDevice& device)
        : m_device(device) {}

    [[nodiscard]] std::optional<BufferDesc> buffer(BufferHandle handle) const override {
        const GlBuffer* buffer = m_device.buffer(handle);
        return buffer != nullptr ? std::optional(buffer->desc()) : std::nullopt;
    }
    [[nodiscard]] std::optional<TextureDesc> texture(TextureHandle handle) const override {
        const GlTexture* texture = m_device.texture(handle);
        return texture != nullptr ? std::optional(texture->desc()) : std::nullopt;
    }
    [[nodiscard]] std::optional<SamplerDesc> sampler(SamplerHandle /*handle*/) const override {
        return std::nullopt; // no samplers yet (#50)
    }
    [[nodiscard]] std::optional<ShaderModuleInfo> shaderModule(ShaderModuleHandle handle) const override {
        const GlShaderModule* shader = m_device.shaderModule(handle);
        if (shader == nullptr) {
            return std::nullopt;
        }
        return ShaderModuleInfo{
            .stage = shader->stage, .reflection = &shader->reflection, .debugName = shader->debugName};
    }
    [[nodiscard]] std::optional<BindGroupLayoutDesc>
    bindGroupLayout(BindGroupLayoutHandle handle) const override {
        const StoredBindGroupLayout* layout = m_device.bindGroupLayout(handle);
        return layout != nullptr ? std::optional(layout->desc()) : std::nullopt;
    }
    [[nodiscard]] std::optional<PipelineLayoutInfo>
    pipelineLayout(PipelineLayoutHandle handle) const override {
        const StoredPipelineLayout* layout = m_device.pipelineLayout(handle);
        return layout != nullptr ? std::optional(layout->info()) : std::nullopt;
    }
    [[nodiscard]] std::optional<BindGroupInfo> bindGroup(BindGroupHandle handle) const override {
        const StoredBindGroup* group = m_device.bindGroup(handle);
        return group != nullptr ? std::optional(group->info()) : std::nullopt;
    }
    [[nodiscard]] std::optional<GraphicsPipelineDesc>
    graphicsPipeline(GraphicsPipelineHandle handle) const override {
        const GlPipeline* pipeline = m_device.pipeline(handle);
        return pipeline != nullptr ? std::optional(pipeline->desc()) : std::nullopt;
    }
    [[nodiscard]] std::optional<PipelineLayoutInfo>
    graphicsPipelineLayout(GraphicsPipelineHandle handle) const override {
        const GlPipeline* pipeline = m_device.pipeline(handle);
        return pipeline != nullptr ? std::optional(pipeline->layout.info()) : std::nullopt;
    }

private:
    const GlDevice& m_device;
};

// The info log of a shader or program, without trailing line breaks.
std::string infoLog(GLuint object, PFNGLGETSHADERIVPROC getParameter, PFNGLGETSHADERINFOLOGPROC getLog) {
    GLint length = 0;
    getParameter(object, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<usize>(std::max(length, 1)), '\0');
    GLsizei written = 0;
    getLog(object, static_cast<GLsizei>(log.size()), &written, log.data());
    log.resize(static_cast<usize>(written));
    while (!log.empty() && (log.back() == '\n' || log.back() == '\r')) {
        log.pop_back();
    }
    return log;
}

std::string_view stageName(ShaderStage stage) noexcept {
    switch (stage) {
        case ShaderStage::Vertex:
            return "vertex";
        case ShaderStage::Fragment:
            return "fragment";
        case ShaderStage::Compute:
            return "compute";
        default:
            return "multi-stage";
    }
}

std::string_view glString(const GladGLContext& gl, GLenum name) {
    const GLubyte* value = gl.GetString(name);
    return value != nullptr ? std::string_view(reinterpret_cast<const char*>(value))
                            : std::string_view("unknown");
}

} // namespace

Result<std::unique_ptr<Device>> GlDevice::create(const DeviceDesc& desc, platform::Window* window) {
    if (window == nullptr) {
        return fail(ErrorCode::Unsupported, Module::Rhi, "The OpenGL backend needs a window");
    }
    platform::OpenGlContext* context = window->openGlContext();
    if (context == nullptr) {
        return fail(ErrorCode::Unsupported, Module::Rhi,
                    "The OpenGL backend needs a window created with GraphicsApi::OpenGL");
    }

    auto device = std::make_unique<GlDevice>(*context);
    TESERRA_TRY(device->initialize(desc, *window));
    return device;
}

GlDevice::GlDevice(platform::OpenGlContext& context)
    : m_context(context), m_thread(std::this_thread::get_id()) {
}

GlDevice::~GlDevice() {
    if (m_gl.DeleteFramebuffers == nullptr) {
        return; // initialization failed before any object was created
    }
    requireContextThread();
    m_context.makeCurrent();
    m_swapchain.reset();
    for (const CachedFramebuffer& framebuffer : m_framebuffers) {
        m_gl.DeleteFramebuffers(1, &framebuffer.name);
    }
    m_textures.forEach(
        [&](TextureHandle /*handle*/, const GlTexture& texture) { m_gl.DeleteTextures(1, &texture.name); });
    m_buffers.forEach(
        [&](BufferHandle /*handle*/, const GlBuffer& buffer) { m_gl.DeleteBuffers(1, &buffer.name); });
    m_pipelines.forEach([&](GraphicsPipelineHandle /*handle*/, const GlPipeline& pipeline) {
        m_gl.DeleteVertexArrays(1, &pipeline.vertexArray);
    });
    for (const CachedProgram& cached : m_programs) {
        m_gl.DeleteProgram(cached.program);
    }
    m_shaderModules.forEach([&](ShaderModuleHandle /*handle*/, const GlShaderModule& shaderModule) {
        m_gl.DeleteShader(shaderModule.shader);
    });
}

Result<void> GlDevice::initialize(const DeviceDesc& desc, platform::Window& window) {
    m_context.makeCurrent();
    const int version = gladLoadGLContextUserPtr(&m_gl, &loadFunction, &m_context);
    if (version == 0) {
        return fail(ErrorCode::DeviceFailure, Module::Rhi, "Could not load the OpenGL functions");
    }
    const int major = GLAD_VERSION_MAJOR(version);
    const int minor = GLAD_VERSION_MINOR(version);
    if (major < 4 || (major == 4 && minor < 5)) {
        return fail(ErrorCode::Unsupported, Module::Rhi,
                    "OpenGL 4.5 is required, but the context provides {}.{} ({})", major, minor,
                    glString(m_gl, GL_RENDERER));
    }
    TESERRA_TRY(queryLimits());
    m_validate = desc.enableValidation;

    if (desc.enableBackendDebug) {
        // Synchronous output logs a message while the call that caused it is still on the stack.
        m_gl.Enable(GL_DEBUG_OUTPUT);
        m_gl.Enable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        m_gl.DebugMessageCallback(&logDebugMessage, nullptr);
        // Drivers echo every debug group the command lists push and pop; those are markers, not messages.
        for (const GLenum type : {GL_DEBUG_TYPE_PUSH_GROUP, GL_DEBUG_TYPE_POP_GROUP}) {
            m_gl.DebugMessageControl(GL_DONT_CARE, type, GL_DONT_CARE, 0, nullptr, GL_FALSE);
        }
    }
    // The RHI's clip-space and framebuffer conventions (ADR-0024): depth [0, 1], row 0 at the top.
    m_gl.ClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);
    // Shaders and clear colors work in linear values; writes to sRGB textures encode them.
    m_gl.Enable(GL_FRAMEBUFFER_SRGB);
    // From here on, the cache knows every piece of pipeline state.
    m_stateCache.reset();

    m_swapchain = std::make_unique<GlSwapchain>(*this, m_context, desc.swapchain, window.framebufferSize());

    TESERRA_LOG_INFO(LogCategory::Rhi, "OpenGL device: {} ({}), OpenGL {}", glString(m_gl, GL_RENDERER),
                     glString(m_gl, GL_VENDOR), glString(m_gl, GL_VERSION));
    return {};
}

Result<void> GlDevice::queryLimits() {
    struct Minimum {
        GLenum name;
        std::string_view text;
        i32 minimum;
    };
    // The flat binding scheme (setIndex * 8 + binding) needs 32 of each (docs/architecture/rhi.md), and a
    // device never reports less than the DeviceLimits defaults.
    const DeviceLimits guaranteed{};
    const std::array minimums{
        Minimum{GL_MAX_UNIFORM_BUFFER_BINDINGS, "GL_MAX_UNIFORM_BUFFER_BINDINGS", 32},
        Minimum{GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, "GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS", 32},
        Minimum{GL_MAX_TEXTURE_SIZE, "GL_MAX_TEXTURE_SIZE",
                static_cast<i32>(guaranteed.maxTextureDimension2D)},
        Minimum{GL_MAX_ARRAY_TEXTURE_LAYERS, "GL_MAX_ARRAY_TEXTURE_LAYERS",
                static_cast<i32>(guaranteed.maxTextureArrayLayers)},
        Minimum{GL_MAX_UNIFORM_BLOCK_SIZE, "GL_MAX_UNIFORM_BLOCK_SIZE",
                static_cast<i32>(guaranteed.maxUniformBufferRange)},
    };
    for (const Minimum& minimum : minimums) {
        const i32 value = getInteger(minimum.name);
        if (value < minimum.minimum) {
            return fail(ErrorCode::Unsupported, Module::Rhi,
                        "{} is {}, but the OpenGL backend needs at least {}", minimum.text, value,
                        minimum.minimum);
        }
    }
    const i32 alignment = getInteger(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT);
    if (alignment <= 0 || alignment > static_cast<i32>(guaranteed.uniformBufferOffsetAlignment)) {
        return fail(ErrorCode::Unsupported, Module::Rhi,
                    "GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT is {}, but the OpenGL backend needs at most {}",
                    alignment, guaranteed.uniformBufferOffsetAlignment);
    }

    m_limits.maxTextureDimension2D = static_cast<u32>(getInteger(GL_MAX_TEXTURE_SIZE));
    m_limits.maxTextureArrayLayers = static_cast<u32>(getInteger(GL_MAX_ARRAY_TEXTURE_LAYERS));
    m_limits.uniformBufferOffsetAlignment = static_cast<u32>(alignment);
    m_limits.maxUniformBufferRange = static_cast<u32>(getInteger(GL_MAX_UNIFORM_BLOCK_SIZE));
    m_limits.maxSamplerAnisotropy = 1.0F;
    if (m_gl.EXT_texture_filter_anisotropic != 0) {
        GLfloat anisotropy = 1.0F;
        m_gl.GetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &anisotropy);
        m_limits.maxSamplerAnisotropy = anisotropy >= 1.0F ? anisotropy : 1.0F;
    }
    return {};
}

i32 GlDevice::getInteger(GLenum name) const {
    GLint value = 0;
    m_gl.GetIntegerv(name, &value);
    return value;
}

// ---------------------------------------------------------------------------------------------------------
// Buffers: immutable storage, persistently mapped when the CPU accesses them.

Result<BufferHandle> GlDevice::createBuffer(const BufferDesc& desc) {
    requireContextThread();
    if (m_validate) {
        validateBufferDesc(desc);
    }
    // Coherent mappings make CPU writes visible to later GPU commands, and GPU writes visible to the CPU
    // once the commands have completed, without barriers.
    GLbitfield access = 0;
    GLbitfield storage = 0;
    switch (desc.memory) {
        case MemoryLocation::GpuOnly:
            storage = GL_DYNAMIC_STORAGE_BIT; // for writeBuffer()
            break;
        case MemoryLocation::Upload:
            access = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            storage = access;
            break;
        case MemoryLocation::Readback:
            access = GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            storage = access | GL_CLIENT_STORAGE_BIT; // cached memory for the CPU to read
            break;
    }

    GLuint name = 0;
    m_gl.CreateBuffers(1, &name);
    m_gl.NamedBufferStorage(name, static_cast<GLsizeiptr>(desc.sizeInBytes), nullptr, storage);
    if (m_gl.GetError() == GL_OUT_OF_MEMORY) {
        m_gl.DeleteBuffers(1, &name);
        return fail(ErrorCode::OutOfMemory, Module::Rhi, "Out of GPU memory for buffer '{}' ({} bytes)",
                    desc.debugName, desc.sizeInBytes);
    }
    void* mapped = nullptr;
    if (access != 0) {
        mapped = m_gl.MapNamedBufferRange(name, 0, static_cast<GLsizeiptr>(desc.sizeInBytes), access);
        if (mapped == nullptr) {
            m_gl.DeleteBuffers(1, &name);
            return fail(ErrorCode::DeviceFailure, Module::Rhi, "Could not map buffer '{}' ({} bytes)",
                        desc.debugName, desc.sizeInBytes);
        }
    }
    if (!desc.debugName.empty()) {
        m_gl.ObjectLabel(GL_BUFFER, name, static_cast<GLsizei>(desc.debugName.size()), desc.debugName.data());
    }
    return m_buffers.create(GlBuffer{
        .name = name,
        .size = desc.sizeInBytes,
        .usage = desc.usage,
        .memory = desc.memory,
        .mapped = static_cast<std::byte*>(mapped),
        .debugName = std::string(desc.debugName),
    });
}

void GlDevice::destroy(BufferHandle handle) {
    if (handle.isNull()) {
        return;
    }
    requireContextThread();
    const GlBuffer* buffer = m_buffers.tryGet(handle);
    TESERRA_ASSERT(buffer != nullptr, "Destroying buffer {}, which is not alive", handle);
    if (buffer == nullptr) {
        return;
    }
    // Deleting also unmaps; GL keeps the storage until the submitted commands that use it have completed.
    m_gl.DeleteBuffers(1, &buffer->name);
    m_buffers.destroy(handle);
}

void GlDevice::writeBuffer(BufferHandle handle, u64 offset, std::span<const std::byte> data) {
    requireContextThread();
    const GlBuffer* buffer = m_buffers.tryGet(handle);
    TESERRA_ASSERT(buffer != nullptr, "writeBuffer to buffer {}, which is not alive", handle);
    if (buffer == nullptr) {
        return;
    }
    if (m_validate) {
        validateWriteBuffer(buffer->desc(), offset, data.size());
    }
    if (!data.empty()) {
        // GL orders it after the commands already replayed and before later ones.
        m_gl.NamedBufferSubData(buffer->name, static_cast<GLintptr>(offset),
                                static_cast<GLsizeiptr>(data.size()), data.data());
    }
}

std::span<std::byte> GlDevice::map(BufferHandle handle) {
    const GlBuffer* buffer = m_buffers.tryGet(handle);
    TESERRA_ASSERT(buffer != nullptr, "map() of buffer {}, which is not alive", handle);
    if (buffer == nullptr) {
        return {};
    }
    if (m_validate) {
        validateMapBuffer(buffer->desc());
    }
    if (buffer->mapped == nullptr) {
        return {};
    }
    return {buffer->mapped, static_cast<usize>(buffer->size)};
}

// ---------------------------------------------------------------------------------------------------------
// Shader modules: GLSL compiled per stage; pipelines link them into programs (#34).

Result<ShaderModuleHandle> GlDevice::createShaderModule(const ShaderModuleDesc& desc) {
    requireContextThread();
    if (m_validate) {
        validateShaderModuleDesc(desc);
    }
    Result<ShaderBlobContents> contents = readShaderModuleBlob(desc);
    if (!contents) {
        return Unexpected(std::move(contents).error());
    }
    // The GLSL for OpenGL in the .tshader blob; it points into desc.blob, which lives for this call.
    const std::string_view source = contents->glsl;
    if (source.empty()) {
        return fail(ErrorCode::Unsupported, Module::Rhi,
                    "Shader module '{}': the blob has no GLSL for OpenGL", desc.debugName);
    }
    if (desc.entryPoint != "main") {
        return fail(ErrorCode::Unsupported, Module::Rhi,
                    "Shader module '{}': GLSL has no entry point '{}'; it must be 'main'", desc.debugName,
                    desc.entryPoint);
    }

    const GLuint shader =
        m_gl.CreateShader(desc.stage == ShaderStage::Fragment ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
    const GLchar* text = source.data();
    const auto length = static_cast<GLint>(source.size());
    m_gl.ShaderSource(shader, 1, &text, &length);
    m_gl.CompileShader(shader);
    GLint compiled = GL_FALSE;
    m_gl.GetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        const std::string log = infoLog(shader, m_gl.GetShaderiv, m_gl.GetShaderInfoLog);
        m_gl.DeleteShader(shader);
        // A shader is content, not code: report the driver's log, whose line numbers are the source's.
        Unexpected error =
            fail(ErrorCode::CompilationFailed, Module::Rhi, "Shader module '{}' ({}) failed to compile:\n{}",
                 desc.debugName, stageName(desc.stage), log);
        TESERRA_LOG_ERROR(LogCategory::Rhi, "{}", error.value().message);
        return error;
    }
    if (!desc.debugName.empty()) {
        m_gl.ObjectLabel(GL_SHADER, shader, static_cast<GLsizei>(desc.debugName.size()),
                         desc.debugName.data());
    }
    return m_shaderModules.create(GlShaderModule{.shader = shader,
                                                 .stage = desc.stage,
                                                 .reflection = std::move(contents->reflection),
                                                 .debugName = std::string(desc.debugName)});
}

void GlDevice::destroy(ShaderModuleHandle handle) {
    if (handle.isNull()) {
        return;
    }
    requireContextThread();
    const GlShaderModule* shaderModule = m_shaderModules.tryGet(handle);
    TESERRA_ASSERT(shaderModule != nullptr, "Destroying shader module {}, which is not alive", handle);
    if (shaderModule == nullptr) {
        return;
    }
    m_gl.DeleteShader(shaderModule->shader);
    m_shaderModules.destroy(handle);
}

// ---------------------------------------------------------------------------------------------------------
// Bind group layouts, pipeline layouts and bind groups: GL has no such objects, so the device keeps the
// descriptors, and replaying bindGroup binds each buffer at its flatBinding(set, binding).

Result<BindGroupLayoutHandle> GlDevice::createBindGroupLayout(const BindGroupLayoutDesc& desc) {
    requireContextThread();
    if (m_validate) {
        validateBindGroupLayoutDesc(desc);
    }
    for (const BindGroupLayoutEntry& entry : desc.entries) {
        if (entry.type == BindingType::StorageBuffer) {
            // TODO(#190): storage buffers, with their own offset alignment and binding count
            return fail(
                ErrorCode::Unsupported, Module::Rhi,
                "Bind group layout '{}': the OpenGL backend cannot bind storage buffers yet (binding {})",
                desc.debugName, entry.binding);
        }
    }
    return m_bindGroupLayouts.create(StoredBindGroupLayout::from(desc));
}

Result<PipelineLayoutHandle> GlDevice::createPipelineLayout(const PipelineLayoutDesc& desc) {
    requireContextThread();
    if (m_validate) {
        validatePipelineLayoutDesc(desc, GlLookup(*this));
    }
    std::array<const StoredBindGroupLayout*, kMaxBindGroups> layouts{};
    for (usize set = 0; set < desc.groups.size() && set < kMaxBindGroups; ++set) {
        layouts[set] = m_bindGroupLayouts.tryGet(desc.groups[set]);
    }
    return m_pipelineLayouts.create(StoredPipelineLayout::from(desc.debugName, layouts));
}

Result<BindGroupHandle> GlDevice::createBindGroup(const BindGroupDesc& desc) {
    requireContextThread();
    if (m_validate) {
        validateBindGroupDesc(desc, GlLookup(*this), m_limits);
    }
    return m_bindGroups.create(StoredBindGroup::from(desc, m_bindGroupLayouts.tryGet(desc.layout)));
}

void GlDevice::destroy(BindGroupLayoutHandle handle) {
    if (!handle.isNull()) {
        TESERRA_ASSERT(m_bindGroupLayouts.contains(handle),
                       "Destroying bind group layout {}, which is not alive", handle);
        m_bindGroupLayouts.destroy(handle);
    }
}

void GlDevice::destroy(PipelineLayoutHandle handle) {
    if (!handle.isNull()) {
        TESERRA_ASSERT(m_pipelineLayouts.contains(handle),
                       "Destroying pipeline layout {}, which is not alive", handle);
        m_pipelineLayouts.destroy(handle);
    }
}

void GlDevice::destroy(BindGroupHandle handle) {
    if (!handle.isNull()) {
        TESERRA_ASSERT(m_bindGroups.contains(handle), "Destroying bind group {}, which is not alive", handle);
        m_bindGroups.destroy(handle);
    }
}

// ---------------------------------------------------------------------------------------------------------
// Graphics pipelines: a shared program, a vertex array per pipeline and captured fixed-function state.

Result<GraphicsPipelineHandle> GlDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    requireContextThread();
    if (m_validate) {
        validateGraphicsPipelineDesc(desc);
        validateGraphicsPipelineResources(desc, GlLookup(*this));
    }
    const GlShaderModule* vertex = m_shaderModules.tryGet(desc.vertexShader);
    const GlShaderModule* fragment =
        desc.fragmentShader.isNull() ? nullptr : m_shaderModules.tryGet(desc.fragmentShader);
    const bool vertexValid = vertex != nullptr && vertex->stage == ShaderStage::Vertex;
    const bool fragmentValid =
        desc.fragmentShader.isNull() || (fragment != nullptr && fragment->stage == ShaderStage::Fragment);
    TESERRA_ASSERT(vertexValid, "Graphics pipeline '{}': vertex shader {} is not a live vertex shader",
                   desc.debugName, desc.vertexShader);
    TESERRA_ASSERT(fragmentValid, "Graphics pipeline '{}': fragment shader {} is not a live fragment shader",
                   desc.debugName, desc.fragmentShader);
    if (!vertexValid || !fragmentValid) {
        return fail(ErrorCode::InvalidArgument, Module::Rhi,
                    "Graphics pipeline '{}' needs a live vertex shader and, if any, a live fragment shader",
                    desc.debugName);
    }
    TESERRA_TRY_ASSIGN(const GLuint program, acquireProgram(desc, *vertex, fragment));

    GLuint vertexArray = 0;
    m_gl.CreateVertexArrays(1, &vertexArray);
    for (const VertexAttribute& attribute : desc.vertexAttributes) {
        const GlVertexFormat format = glVertexFormat(attribute.format);
        m_gl.EnableVertexArrayAttrib(vertexArray, attribute.location);
        if (format.integer) {
            m_gl.VertexArrayAttribIFormat(vertexArray, attribute.location, format.components, format.type,
                                          attribute.offset);
        } else {
            m_gl.VertexArrayAttribFormat(vertexArray, attribute.location, format.components, format.type,
                                         format.normalized ? GL_TRUE : GL_FALSE, attribute.offset);
        }
        m_gl.VertexArrayAttribBinding(vertexArray, attribute.location, attribute.bufferIndex);
    }
    GlPipeline pipeline{
        .program = program,
        .vertexArray = vertexArray,
        .state = capturePipelineState(desc),
        .primitiveMode = glPrimitiveMode(desc.topology),
        .stored = desc,
        .layout = {}, // copied from the pipeline layout below
        .debugName = std::string(desc.debugName),
    };
    for (u32 i = 0; i < desc.vertexBuffers.size() && i < kMaxVertexBuffers; ++i) {
        const VertexBufferLayout& layout = desc.vertexBuffers[i];
        m_gl.VertexArrayBindingDivisor(vertexArray, i,
                                       layout.stepRate == VertexStepRate::PerInstance ? 1U : 0U);
        pipeline.vertexBuffers[i] = layout;
        pipeline.vertexBufferCount = i + 1;
    }
    if (!desc.debugName.empty()) {
        m_gl.ObjectLabel(GL_VERTEX_ARRAY, vertexArray, static_cast<GLsizei>(desc.debugName.size()),
                         desc.debugName.data());
    }
    if (desc.raster.depthBias.clamp != 0.0F) {
        TESERRA_LOG_WARN(LogCategory::Rhi,
                         "Graphics pipeline '{}': the depth bias clamp needs OpenGL 4.6 and is ignored",
                         desc.debugName);
    }
    // The views of the caller's descriptor are only valid during this call.
    pipeline.stored.vertexBuffers = {};
    pipeline.stored.vertexAttributes = {};
    pipeline.stored.debugName = {};
    if (const StoredPipelineLayout* layout = m_pipelineLayouts.tryGet(desc.layout)) {
        pipeline.layout = *layout;
    }
    return m_pipelines.create(std::move(pipeline));
}

Result<GLuint> GlDevice::acquireProgram(const GraphicsPipelineDesc& desc, const GlShaderModule& vertex,
                                        const GlShaderModule* fragment) {
    const auto cached = std::ranges::find_if(m_programs, [&](const CachedProgram& entry) {
        return entry.vertexShader == desc.vertexShader && entry.fragmentShader == desc.fragmentShader;
    });
    if (cached != m_programs.end()) {
        ++cached->users;
        return cached->program;
    }

    const GLuint program = m_gl.CreateProgram();
    m_gl.AttachShader(program, vertex.shader);
    if (fragment != nullptr) {
        m_gl.AttachShader(program, fragment->shader);
    }
    m_gl.LinkProgram(program);
    // A linked program keeps working without its shaders, which stay independent of pipelines.
    m_gl.DetachShader(program, vertex.shader);
    if (fragment != nullptr) {
        m_gl.DetachShader(program, fragment->shader);
    }
    GLint linked = GL_FALSE;
    m_gl.GetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        const std::string log = infoLog(program, m_gl.GetProgramiv, m_gl.GetProgramInfoLog);
        m_gl.DeleteProgram(program);
        Unexpected error = fail(ErrorCode::CompilationFailed, Module::Rhi,
                                "Graphics pipeline '{}' failed to link:\n{}", desc.debugName, log);
        TESERRA_LOG_ERROR(LogCategory::Rhi, "{}", error.value().message);
        return error;
    }
    const std::string label = fragment != nullptr
                                  ? std::format("{} + {}", vertex.debugName, fragment->debugName)
                                  : vertex.debugName;
    if (!label.empty()) {
        m_gl.ObjectLabel(GL_PROGRAM, program, static_cast<GLsizei>(label.size()), label.data());
    }
    m_programs.push_back({.vertexShader = desc.vertexShader,
                          .fragmentShader = desc.fragmentShader,
                          .program = program,
                          .users = 1});
    return program;
}

void GlDevice::releaseProgram(GLuint program) {
    const auto cached = std::ranges::find(m_programs, program, &CachedProgram::program);
    TESERRA_ASSERT(cached != m_programs.end(), "Program {} is not in the cache", program);
    if (cached == m_programs.end() || --cached->users > 0) {
        return;
    }
    m_stateCache.unbind(program, 0);
    m_gl.DeleteProgram(program);
    m_programs.erase(cached);
}

void GlDevice::destroy(GraphicsPipelineHandle handle) {
    if (handle.isNull()) {
        return;
    }
    requireContextThread();
    const GlPipeline* pipeline = m_pipelines.tryGet(handle);
    TESERRA_ASSERT(pipeline != nullptr, "Destroying graphics pipeline {}, which is not alive", handle);
    if (pipeline == nullptr) {
        return;
    }
    m_stateCache.unbind(0, pipeline->vertexArray);
    m_gl.DeleteVertexArrays(1, &pipeline->vertexArray);
    releaseProgram(pipeline->program);
    m_pipelines.destroy(handle);
}

// ---------------------------------------------------------------------------------------------------------
// Textures

Result<TextureHandle> GlDevice::createTexture(const TextureDesc& desc) {
    requireContextThread();
    if (m_validate) {
        validateTextureDesc(desc, m_limits);
    }
    if (desc.dimension == TextureDimension::Tex3D || desc.dimension == TextureDimension::Cube) {
        return fail(ErrorCode::Unsupported, Module::Rhi,
                    "Texture '{}': the OpenGL backend has no 3D or cube textures in v1", desc.debugName);
    }
    if (desc.sampleCount != 1) {
        return fail(ErrorCode::Unsupported, Module::Rhi,
                    "Texture '{}': the OpenGL backend has no multisampled textures in v1", desc.debugName);
    }
    TextureDesc resolved = desc;
    resolved.mipLevels = mipLevelCount(desc);
    return allocateTexture(resolved);
}

Result<TextureHandle> GlDevice::allocateTexture(const TextureDesc& desc) {
    const bool array = desc.dimension == TextureDimension::Tex2DArray;
    GLuint name = 0;
    m_gl.CreateTextures(array ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D, 1, &name);
    // Immutable storage for every mip level at once.
    const auto levels = static_cast<GLsizei>(desc.mipLevels);
    const auto width = static_cast<GLsizei>(desc.width);
    const auto height = static_cast<GLsizei>(desc.height);
    if (array) {
        m_gl.TextureStorage3D(name, levels, glInternalFormat(desc.format), width, height,
                              static_cast<GLsizei>(desc.depthOrLayers));
    } else {
        m_gl.TextureStorage2D(name, levels, glInternalFormat(desc.format), width, height);
    }
    if (m_gl.GetError() == GL_OUT_OF_MEMORY) {
        m_gl.DeleteTextures(1, &name);
        return fail(ErrorCode::OutOfMemory, Module::Rhi, "Out of GPU memory for texture '{}' ({}x{}x{})",
                    desc.debugName, desc.width, desc.height, desc.depthOrLayers);
    }
    m_gl.ObjectLabel(GL_TEXTURE, name, static_cast<GLsizei>(desc.debugName.size()), desc.debugName.data());
    return m_textures.create(GlTexture{
        .name = name,
        .dimension = desc.dimension,
        .format = desc.format,
        .size = {desc.width, desc.height},
        .layers = desc.depthOrLayers,
        .mipLevels = desc.mipLevels,
        .usage = desc.usage,
        .debugName = std::string(desc.debugName),
    });
}

void GlDevice::destroy(TextureHandle handle) {
    if (handle.isNull()) {
        return;
    }
    requireContextThread();
    releaseTexture(handle);
}

void GlDevice::writeTexture(TextureHandle handle, const TextureRegion& region,
                            std::span<const std::byte> data, u32 bytesPerRow) {
    requireContextThread();
    const GlTexture* texture = m_textures.tryGet(handle);
    TESERRA_ASSERT(texture != nullptr, "writeTexture to texture {}, which is not alive", handle);
    if (texture == nullptr) {
        return;
    }
    if (m_validate) {
        validateWriteTexture(texture->desc(), region, data.size(), bytesPerRow);
    }
    // GL orders it after the commands already replayed and before later ones.
    uploadTexels(*texture, region, bytesPerRow, data.data());
}

void GlDevice::uploadTexels(const GlTexture& texture, const TextureRegion& region, u32 bytesPerRow,
                            const void* pixels) {
    // Rows are read top row first into GL's first row, which is the top under the upper-left clip origin
    // (ADR-0024): no flip. The row length is in texels; 0 means tightly packed.
    const u32 texelSize = formatInfo(texture.format).bytesPerTexel;
    m_gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    m_gl.PixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(bytesPerRow / texelSize));
    const GlPixelFormat pixel = glPixelFormat(texture.format);
    const auto level = static_cast<GLint>(region.mipLevel);
    const auto x = static_cast<GLint>(region.x);
    const auto y = static_cast<GLint>(region.y);
    const auto width = static_cast<GLsizei>(region.width);
    const auto height = static_cast<GLsizei>(region.height);
    if (texture.dimension == TextureDimension::Tex2DArray) {
        m_gl.TextureSubImage3D(texture.name, level, x, y, static_cast<GLint>(region.arrayLayer), width,
                               height, static_cast<GLsizei>(region.layerCount), pixel.format, pixel.type,
                               pixels);
    } else {
        m_gl.TextureSubImage2D(texture.name, level, x, y, width, height, pixel.format, pixel.type, pixels);
    }
}

void GlDevice::generateMipmaps(TextureHandle handle) {
    requireContextThread();
    const GlTexture* texture = m_textures.tryGet(handle);
    TESERRA_ASSERT(texture != nullptr, "generateMipmaps of texture {}, which is not alive", handle);
    if (texture == nullptr) {
        return;
    }
    if (m_validate) {
        validateGenerateMipmaps(texture->desc());
    }
    // Drivers filter sRGB textures in linear space, as GL requires; the GPU test checks it.
    m_gl.GenerateTextureMipmap(texture->name);
}

// ---------------------------------------------------------------------------------------------------------
// Other resources: created by the OpenGL backend from iteration 5 on.

Result<SamplerHandle> GlDevice::createSampler(const SamplerDesc& desc) {
    // TODO(#50): samplers
    return fail(ErrorCode::Unsupported, Module::Rhi, "The OpenGL backend cannot create samplers yet ('{}')",
                desc.debugName);
}

// Nothing else can be created yet, so only null handles can be destroyed.
void GlDevice::destroy(SamplerHandle handle) {
    TESERRA_ASSERT(handle.isNull(), "Destroying sampler {}, which this device did not create", handle);
}

// ---------------------------------------------------------------------------------------------------------
// Frames and submission

void GlDevice::beginFrame() {
    requireContextThread();
    TESERRA_ASSERT(!m_inFrame, "beginFrame() twice without endFrame()");
    // TODO(#56): wait for the frame slot's fence before reusing its command lists
    for (CommandList& list : m_commandLists[m_frame % kMaxFramesInFlight]) {
        list.reset();
    }
    m_commandListsInUse = 0;
    m_inFrame = true;
}

void GlDevice::endFrame() {
    requireContextThread();
    TESERRA_ASSERT(m_inFrame, "endFrame() without beginFrame()");
    m_inFrame = false;
    ++m_frame;
}

CommandList& GlDevice::acquireCommandList() {
    TESERRA_ASSERT(m_inFrame, "acquireCommandList() outside beginFrame() / endFrame()");
    std::deque<CommandList>& lists = m_commandLists[m_frame % kMaxFramesInFlight];
    if (m_commandListsInUse == lists.size()) {
        lists.emplace_back();
    }
    return lists[m_commandListsInUse++];
}

void GlDevice::submit(CommandList& commands) {
    requireContextThread();
    commands.finish();
    if (m_validate) {
        validateCommands(commands, GlLookup(*this), m_limits);
    }
    replay(commands);
}

Swapchain* GlDevice::swapchain() noexcept {
    return m_swapchain.get();
}

// ---------------------------------------------------------------------------------------------------------
// Replay

void GlDevice::replay(const CommandList& commands) {
    for (const Command command : commands.commands()) {
        switch (command.type()) {
            case CommandType::BeginRenderPass:
                beginRenderPass(command.as<BeginRenderPassCommand>().pass);
                break;
            case CommandType::EndRenderPass:
                endRenderPass();
                break;
            case CommandType::SetViewport: {
                // With GL_UPPER_LEFT clip control, window y counts from row 0, the top: no flip.
                const Viewport& viewport = command.as<SetViewportCommand>().viewport;
                m_gl.ViewportIndexedf(0, viewport.x, viewport.y, viewport.width, viewport.height);
                m_gl.DepthRangef(viewport.minDepth, viewport.maxDepth);
                break;
            }
            case CommandType::SetScissor: {
                const ScissorRect& scissor = command.as<SetScissorCommand>().scissor;
                m_gl.Enable(GL_SCISSOR_TEST);
                m_gl.Scissor(static_cast<GLint>(scissor.x), static_cast<GLint>(scissor.y),
                             static_cast<GLsizei>(scissor.width), static_cast<GLsizei>(scissor.height));
                break;
            }
            case CommandType::PushDebugGroup: {
                const std::string_view label = command.debugLabel();
                m_gl.PushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, static_cast<GLsizei>(label.size()),
                                    label.data());
                break;
            }
            case CommandType::PopDebugGroup:
                m_gl.PopDebugGroup();
                break;
            case CommandType::BindPipeline:
                bindPipeline(command.as<BindPipelineCommand>().pipeline);
                break;
            case CommandType::BindVertexBuffers:
                bindVertexBuffers(command.as<BindVertexBuffersCommand>().firstSlot, command.vertexBuffers());
                break;
            case CommandType::BindGroup: {
                const auto& bind = command.as<BindGroupCommand>();
                applyBindGroup(bind.index, bind.group, command.dynamicOffsets());
                break;
            }
            case CommandType::BindIndexBuffer:
                bindIndexBuffer(command.as<BindIndexBufferCommand>());
                break;
            case CommandType::Draw:
                draw(command.as<DrawCommand>().args);
                break;
            case CommandType::DrawIndexed:
                drawIndexed(command.as<DrawIndexedCommand>().args);
                break;
            case CommandType::CopyBuffer:
                copyBuffer(command.as<CopyBufferCommand>().copy);
                break;
            case CommandType::CopyBufferToTexture:
                copyBufferToTexture(command.as<CopyBufferToTextureCommand>().copy);
                break;
            default:
                // TODO(#51): copyTextureToBuffer, for readback
                TESERRA_ASSERT(false, "The OpenGL backend cannot replay {} commands yet",
                               commandTypeName(command.type()));
                break;
        }
    }
}

void GlDevice::bindPipeline(GraphicsPipelineHandle handle) {
    const GlPipeline* pipeline = m_pipelines.tryGet(handle);
    TESERRA_ASSERT(pipeline != nullptr, "bindPipeline uses pipeline {}, which is not alive", handle);
    if (pipeline == nullptr) {
        return;
    }
    m_stateCache.bind(pipeline->program, pipeline->vertexArray, pipeline->state);
    m_passPipeline = pipeline;
}

void GlDevice::applyBindGroup(u32 set, BindGroupHandle handle, std::span<const u32> dynamicOffsets) {
    const StoredBindGroup* group = m_bindGroups.tryGet(handle);
    TESERRA_ASSERT(group != nullptr, "bindGroup uses bind group {}, which is not alive", handle);
    if (group == nullptr) {
        return;
    }
    // Uniform buffer binding points are context state, not program state: a group stays bound across
    // pipelines, as the RHI defines. Entries and layout entries are both sorted by binding, and the dynamic
    // offsets follow the dynamic bindings in that order.
    usize nextDynamicOffset = 0;
    for (usize i = 0; i < group->entries.size() && i < group->layoutEntries.size(); ++i) {
        const BindGroupLayoutEntry& binding = group->layoutEntries[i];
        const BindGroupEntry& entry = group->entries[i];
        u64 offset = entry.buffer.offset;
        if (binding.hasDynamicOffset && nextDynamicOffset < dynamicOffsets.size()) {
            offset += dynamicOffsets[nextDynamicOffset++];
        }
        // Only uniform buffers so far: storage buffer layouts are refused (#190), and textures and samplers
        // cannot be created yet (#49, #50).
        if (binding.type != BindingType::UniformBuffer) {
            continue;
        }
        // Looked up now, not at creation: a destroyed buffer's GL name may already belong to another one.
        const GlBuffer* buffer = m_buffers.tryGet(entry.buffer.buffer);
        TESERRA_ASSERT(buffer != nullptr, "bindGroup '{}' uses buffer {} at binding {}, which is not alive",
                       group->debugName, entry.buffer.buffer, entry.binding);
        if (buffer == nullptr) {
            continue;
        }
        const u64 size = entry.buffer.size != 0 ? entry.buffer.size : buffer->size - entry.buffer.offset;
        m_gl.BindBufferRange(GL_UNIFORM_BUFFER, flatBinding(set, entry.binding), buffer->name,
                             static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size));
    }
}

void GlDevice::bindVertexBuffers(u32 firstSlot, std::span<const VertexBufferBinding> bindings) {
    for (usize i = 0; i < bindings.size(); ++i) {
        const GlBuffer* buffer = m_buffers.tryGet(bindings[i].buffer);
        TESERRA_ASSERT(buffer != nullptr, "bindVertexBuffers uses buffer {}, which is not alive",
                       bindings[i].buffer);
        m_passVertexBuffers[firstSlot + i] = {.name = buffer != nullptr ? buffer->name : 0,
                                              .offset = bindings[i].offset};
    }
    m_vertexInputDirty = true;
}

void GlDevice::bindIndexBuffer(const BindIndexBufferCommand& bind) {
    const GlBuffer* buffer = m_buffers.tryGet(bind.buffer);
    TESERRA_ASSERT(buffer != nullptr, "bindIndexBuffer uses buffer {}, which is not alive", bind.buffer);
    m_passIndexBuffer = {.name = buffer != nullptr ? buffer->name : 0, .offset = bind.offset};
    m_passIndexType = bind.indexType;
    m_vertexInputDirty = true;
}

bool GlDevice::flushVertexInput() {
    const GlPipeline* pipeline = m_passPipeline;
    if (pipeline == nullptr) {
        return false; // recording asserts that draws have a pipeline
    }
    if (!m_vertexInputDirty && m_flushedVertexArray == pipeline->vertexArray) {
        return true;
    }
    for (u32 slot = 0; slot < pipeline->vertexBufferCount; ++slot) {
        const BoundBuffer& bound = m_passVertexBuffers[slot];
        TESERRA_ASSERT(bound.name != 0, "draw needs vertex buffer {} of pipeline '{}', which is not bound",
                       slot, pipeline->debugName);
        m_gl.VertexArrayVertexBuffer(pipeline->vertexArray, slot, bound.name,
                                     static_cast<GLintptr>(bound.offset),
                                     static_cast<GLsizei>(pipeline->vertexBuffers[slot].stride));
    }
    if (m_passIndexBuffer.name != 0) {
        m_gl.VertexArrayElementBuffer(pipeline->vertexArray, m_passIndexBuffer.name);
    }
    m_flushedVertexArray = pipeline->vertexArray;
    m_vertexInputDirty = false;
    return true;
}

void GlDevice::draw(const DrawArgs& args) {
    if (!flushVertexInput()) {
        return;
    }
    m_gl.DrawArraysInstancedBaseInstance(m_passPipeline->primitiveMode, static_cast<GLint>(args.firstVertex),
                                         static_cast<GLsizei>(args.vertexCount),
                                         static_cast<GLsizei>(args.instanceCount), args.firstInstance);
}

void GlDevice::drawIndexed(const DrawIndexedArgs& args) {
    if (!flushVertexInput()) {
        return;
    }
    const bool wide = m_passIndexType == IndexType::Uint32;
    const u64 indexSize = wide ? 4 : 2;
    // GL takes the byte offset into the bound element buffer as a pointer.
    const auto offset =
        static_cast<std::uintptr_t>(m_passIndexBuffer.offset + (u64{args.firstIndex} * indexSize));
    m_gl.DrawElementsInstancedBaseVertexBaseInstance(
        m_passPipeline->primitiveMode, static_cast<GLsizei>(args.indexCount),
        wide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
        reinterpret_cast<const void*>(offset), // NOLINT(performance-no-int-to-ptr): GL's offset convention
        static_cast<GLsizei>(args.instanceCount), args.vertexOffset, args.firstInstance);
}

void GlDevice::copyBuffer(const BufferCopy& copy) {
    const GlBuffer* source = m_buffers.tryGet(copy.source);
    const GlBuffer* destination = m_buffers.tryGet(copy.destination);
    TESERRA_ASSERT(source != nullptr, "copyBuffer reads buffer {}, which is not alive", copy.source);
    TESERRA_ASSERT(destination != nullptr, "copyBuffer writes buffer {}, which is not alive",
                   copy.destination);
    if (source == nullptr || destination == nullptr) {
        return;
    }
    m_gl.CopyNamedBufferSubData(source->name, destination->name, static_cast<GLintptr>(copy.sourceOffset),
                                static_cast<GLintptr>(copy.destinationOffset),
                                static_cast<GLsizeiptr>(copy.size));
}

void GlDevice::copyBufferToTexture(const BufferTextureCopy& copy) {
    const GlBuffer* buffer = m_buffers.tryGet(copy.buffer);
    const GlTexture* texture = m_textures.tryGet(copy.texture);
    TESERRA_ASSERT(buffer != nullptr, "copyBufferToTexture reads buffer {}, which is not alive", copy.buffer);
    TESERRA_ASSERT(texture != nullptr, "copyBufferToTexture writes texture {}, which is not alive",
                   copy.texture);
    if (buffer == nullptr || texture == nullptr) {
        return;
    }
    // With a pixel unpack buffer bound, GL reads the texels from it at the byte offset passed as the pointer.
    m_gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer->name);
    const auto offset = static_cast<std::uintptr_t>(copy.bufferOffset);
    uploadTexels(
        *texture,
        {.mipLevel = copy.mipLevel,
         .arrayLayer = copy.arrayLayer,
         .layerCount = copy.layerCount,
         .x = copy.x,
         .y = copy.y,
         .width = copy.width,
         .height = copy.height},
        copy.bytesPerRow,
        reinterpret_cast<const void*>(offset)); // NOLINT(performance-no-int-to-ptr): GL's offset convention
    // writeTexture() reads client memory, which needs no unpack buffer bound.
    m_gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

void GlDevice::beginRenderPass(const RenderPassDesc& pass) {
    FramebufferKey key;
    UVec2 renderArea{};
    const DepthStencilAttachment& depthStencil = pass.depthStencil;
    const GlTexture* depthTexture = nullptr;
    if (!depthStencil.texture.isNull()) {
        depthTexture = m_textures.tryGet(depthStencil.texture);
        TESERRA_ASSERT(depthTexture != nullptr,
                       "The depth-stencil attachment uses texture {}, which is not alive",
                       depthStencil.texture);
        if (depthTexture == nullptr) {
            return;
        }
        key.depthStencil = {.texture = depthStencil.texture,
                            .mipLevel = depthStencil.mipLevel,
                            .arrayLayer = depthStencil.arrayLayer};
        // The render area is the first color attachment's size, or the depth attachment's without one.
        renderArea = {std::max(depthTexture->size.x >> depthStencil.mipLevel, 1U),
                      std::max(depthTexture->size.y >> depthStencil.mipLevel, 1U)};
    }
    for (u32 i = 0; i < kMaxColorAttachments; ++i) {
        const ColorAttachment& attachment = pass.colorAttachments[i];
        if (attachment.texture.isNull()) {
            break;
        }
        const GlTexture* texture = m_textures.tryGet(attachment.texture);
        TESERRA_ASSERT(texture != nullptr, "Color attachment {} uses texture {}, which is not alive", i,
                       attachment.texture);
        if (texture == nullptr) {
            return;
        }
        key.color[i] = {.texture = attachment.texture,
                        .mipLevel = attachment.mipLevel,
                        .arrayLayer = attachment.arrayLayer};
        if (i == 0) {
            renderArea = {std::max(texture->size.x >> attachment.mipLevel, 1U),
                          std::max(texture->size.y >> attachment.mipLevel, 1U)};
        }
    }

    // Each pass starts with nothing bound (docs/architecture/rhi.md#command-recording-and-submission).
    m_passPipeline = nullptr;
    m_passVertexBuffers.fill({});
    m_passIndexBuffer = {};
    m_vertexInputDirty = true;

    m_passFramebuffer = framebuffer(key);
    m_gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, m_passFramebuffer);
    // A pass covers its whole render area until the commands say otherwise.
    m_gl.Disable(GL_SCISSOR_TEST);
    m_gl.ViewportIndexedf(0, 0.0F, 0.0F, static_cast<f32>(renderArea.x), static_cast<f32>(renderArea.y));
    m_gl.DepthRangef(0.0F, 1.0F);

    u32 colorCount = 0;
    while (colorCount < kMaxColorAttachments && !pass.colorAttachments[colorCount].texture.isNull()) {
        ++colorCount;
    }
    // Clears are masked like draws: a pipeline bound earlier must not keep components from being cleared.
    m_stateCache.prepareColorClear(colorCount);
    std::array<GLenum, kMaxColorAttachments + 2> undefined{};
    u32 undefinedCount = 0;
    m_passDiscardCount = 0;
    for (u32 i = 0; i < kMaxColorAttachments && !pass.colorAttachments[i].texture.isNull(); ++i) {
        const ColorAttachment& attachment = pass.colorAttachments[i];
        if (attachment.loadOp == LoadOp::Clear) {
            const std::array color{attachment.clearColor.x, attachment.clearColor.y, attachment.clearColor.z,
                                   attachment.clearColor.w};
            m_gl.ClearNamedFramebufferfv(m_passFramebuffer, GL_COLOR, static_cast<GLint>(i), color.data());
        } else if (attachment.loadOp == LoadOp::DontCare) {
            undefined[undefinedCount++] = GL_COLOR_ATTACHMENT0 + i;
        }
        if (attachment.storeOp == StoreOp::DontCare) {
            m_passDiscards[m_passDiscardCount++] = GL_COLOR_ATTACHMENT0 + i;
        }
    }
    if (depthTexture != nullptr) {
        const FormatInfo& format = formatInfo(depthTexture->format);
        const bool clearDepth = format.hasDepth && depthStencil.depthLoadOp == LoadOp::Clear;
        const bool clearStencil = format.hasStencil && depthStencil.stencilLoadOp == LoadOp::Clear;
        const auto stencil = static_cast<GLint>(depthStencil.clearStencil);
        if (clearDepth) {
            // Depth clears are masked like depth writes.
            m_stateCache.prepareDepthClear();
        }
        if (clearDepth && clearStencil) {
            m_gl.ClearNamedFramebufferfi(m_passFramebuffer, GL_DEPTH_STENCIL, 0, depthStencil.clearDepth,
                                         stencil);
        } else if (clearDepth) {
            m_gl.ClearNamedFramebufferfv(m_passFramebuffer, GL_DEPTH, 0, &depthStencil.clearDepth);
        } else if (clearStencil) {
            m_gl.ClearNamedFramebufferiv(m_passFramebuffer, GL_STENCIL, 0, &stencil);
        }
        // Each aspect is loaded and stored on its own, like on Vulkan.
        if (format.hasDepth && depthStencil.depthLoadOp == LoadOp::DontCare) {
            undefined[undefinedCount++] = GL_DEPTH_ATTACHMENT;
        }
        if (format.hasStencil && depthStencil.stencilLoadOp == LoadOp::DontCare) {
            undefined[undefinedCount++] = GL_STENCIL_ATTACHMENT;
        }
        if (format.hasDepth && depthStencil.depthStoreOp == StoreOp::DontCare) {
            m_passDiscards[m_passDiscardCount++] = GL_DEPTH_ATTACHMENT;
        }
        if (format.hasStencil && depthStencil.stencilStoreOp == StoreOp::DontCare) {
            m_passDiscards[m_passDiscardCount++] = GL_STENCIL_ATTACHMENT;
        }
    }
    if (undefinedCount > 0) {
        m_gl.InvalidateNamedFramebufferData(m_passFramebuffer, static_cast<GLsizei>(undefinedCount),
                                            undefined.data());
    }
}

void GlDevice::endRenderPass() {
    if (m_passDiscardCount > 0) {
        m_gl.InvalidateNamedFramebufferData(m_passFramebuffer, static_cast<GLsizei>(m_passDiscardCount),
                                            m_passDiscards.data());
    }
    m_gl.Disable(GL_SCISSOR_TEST);
    m_passFramebuffer = 0;
    m_passDiscardCount = 0;
}

// ---------------------------------------------------------------------------------------------------------
// Textures and framebuffers

TextureHandle GlDevice::createTexture2D(Format format, UVec2 size, TextureUsage usage,
                                        std::string_view debugName) {
    GLuint name = 0;
    m_gl.CreateTextures(GL_TEXTURE_2D, 1, &name);
    m_gl.TextureStorage2D(name, 1, glInternalFormat(format), static_cast<GLsizei>(size.x),
                          static_cast<GLsizei>(size.y));
    m_gl.ObjectLabel(GL_TEXTURE, name, static_cast<GLsizei>(debugName.size()), debugName.data());
    return m_textures.create(GlTexture{
        .name = name,
        .format = format,
        .size = size,
        .usage = usage,
        .debugName = std::string(debugName),
    });
}

void GlDevice::releaseTexture(TextureHandle handle) {
    const GlTexture* texture = m_textures.tryGet(handle);
    TESERRA_ASSERT(texture != nullptr, "Releasing texture {}, which is not alive", handle);
    if (texture == nullptr) {
        return;
    }
    const auto usesTexture = [&](const CachedFramebuffer& framebuffer) {
        return framebuffer.key.uses(handle);
    };
    for (const CachedFramebuffer& framebuffer : m_framebuffers) {
        if (usesTexture(framebuffer)) {
            m_gl.DeleteFramebuffers(1, &framebuffer.name);
        }
    }
    std::erase_if(m_framebuffers, usesTexture);
    m_gl.DeleteTextures(1, &texture->name);
    m_textures.destroy(handle);
}

GLuint GlDevice::colorFramebuffer(TextureHandle texture) {
    FramebufferKey key;
    key.color[0].texture = texture;
    return framebuffer(key);
}

GLuint GlDevice::framebuffer(const FramebufferKey& key) {
    const auto cached = std::ranges::find(m_framebuffers, key, &CachedFramebuffer::key);
    if (cached != m_framebuffers.end()) {
        return cached->name;
    }

    GLuint name = 0;
    m_gl.CreateFramebuffers(1, &name);
    std::array<GLenum, kMaxColorAttachments> drawBuffers{};
    GLsizei drawBufferCount = 0;
    for (const FramebufferKey::Attachment& attachment : key.color) {
        if (attachment.texture.isNull()) {
            break;
        }
        const GLenum point = GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(drawBufferCount);
        attach(name, point, attachment);
        drawBuffers[static_cast<usize>(drawBufferCount++)] = point;
    }
    if (!key.depthStencil.texture.isNull()) {
        const FormatInfo& format = formatInfo(m_textures.get(key.depthStencil.texture).format);
        attach(name, format.hasStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, key.depthStencil);
    }
    m_gl.NamedFramebufferDrawBuffers(name, drawBufferCount, drawBuffers.data());
    const GLenum status = m_gl.CheckNamedFramebufferStatus(name, GL_DRAW_FRAMEBUFFER);
    TESERRA_ASSERT(status == GL_FRAMEBUFFER_COMPLETE, "Framebuffer of texture {} is incomplete (0x{:X})",
                   key.color[0].texture.isNull() ? key.depthStencil.texture : key.color[0].texture, status);
    m_framebuffers.push_back({.key = key, .name = name});
    return name;
}

void GlDevice::attach(GLuint name, GLenum point, const FramebufferKey::Attachment& attachment) {
    const GlTexture& texture = m_textures.get(attachment.texture);
    const auto level = static_cast<GLint>(attachment.mipLevel);
    // glNamedFramebufferTexture() would attach every layer of an array texture (a layered framebuffer).
    if (texture.dimension == TextureDimension::Tex2DArray) {
        m_gl.NamedFramebufferTextureLayer(name, point, texture.name, level,
                                          static_cast<GLint>(attachment.arrayLayer));
    } else {
        m_gl.NamedFramebufferTexture(name, point, texture.name, level);
    }
}

void GlDevice::waitIdle() {
    requireContextThread();
    m_gl.Finish();
}

void GlDevice::requireContextThread() const {
    TESERRA_ASSERT(std::this_thread::get_id() == m_thread,
                   "OpenGL device used from a thread other than the one that created it");
}

} // namespace teserra::rhi::opengl
