#include "gl_device.hpp"

#include <teserra/core/assert.hpp>
#include <teserra/core/log.hpp>
#include <teserra/platform/opengl_context.hpp>
#include <teserra/platform/window.hpp>
#include <teserra/rhi/validation.hpp>

#include "gl_debug.hpp"
#include "gl_format.hpp"
#include "gl_swapchain.hpp"

#include <algorithm>
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
    // TODO(#34): pipelines
    [[nodiscard]] std::optional<GraphicsPipelineDesc>
    graphicsPipeline(GraphicsPipelineHandle /*handle*/) const override {
        return std::nullopt;
    }

private:
    const GlDevice& m_device;
};

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
// Other resources: created by the OpenGL backend from iteration 5 on.

Result<TextureHandle> GlDevice::createTexture(const TextureDesc& desc) {
    // TODO(#49): textures
    return fail(ErrorCode::Unsupported, Module::Rhi, "The OpenGL backend cannot create textures yet ('{}')",
                desc.debugName);
}

Result<SamplerHandle> GlDevice::createSampler(const SamplerDesc& desc) {
    // TODO(#50): samplers
    return fail(ErrorCode::Unsupported, Module::Rhi, "The OpenGL backend cannot create samplers yet ('{}')",
                desc.debugName);
}

Result<ShaderModuleHandle> GlDevice::createShaderModule(const ShaderModuleDesc& desc) {
    // TODO(#33): shader modules
    return fail(ErrorCode::Unsupported, Module::Rhi,
                "The OpenGL backend cannot create shader modules yet ('{}')", desc.debugName);
}

Result<GraphicsPipelineHandle> GlDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    // TODO(#34): graphics pipelines
    return fail(ErrorCode::Unsupported, Module::Rhi,
                "The OpenGL backend cannot create graphics pipelines yet ('{}')", desc.debugName);
}

// Nothing else can be created yet, so only null handles can be destroyed.
void GlDevice::destroy(TextureHandle handle) {
    TESERRA_ASSERT(handle.isNull(), "Destroying texture {}, which this device did not create", handle);
}

void GlDevice::destroy(SamplerHandle handle) {
    TESERRA_ASSERT(handle.isNull(), "Destroying sampler {}, which this device did not create", handle);
}

void GlDevice::destroy(ShaderModuleHandle handle) {
    TESERRA_ASSERT(handle.isNull(), "Destroying shader module {}, which this device did not create", handle);
}

void GlDevice::destroy(GraphicsPipelineHandle handle) {
    TESERRA_ASSERT(handle.isNull(), "Destroying graphics pipeline {}, which this device did not create",
                   handle);
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
        validateCommands(commands, GlLookup(*this));
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
            case CommandType::CopyBuffer:
                copyBuffer(command.as<CopyBufferCommand>().copy);
                break;
            default:
                // Draws and their bindings follow with pipelines (#34, #35), bind groups with #42 and
                // texture copies with textures (#49).
                TESERRA_ASSERT(false, "The OpenGL backend cannot replay {} commands yet",
                               commandTypeName(command.type()));
                break;
        }
    }
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

void GlDevice::beginRenderPass(const RenderPassDesc& pass) {
    // TODO(#49): depth-stencil attachments, once depth textures can be created
    TESERRA_ASSERT(pass.depthStencil.texture.isNull(),
                   "The OpenGL backend has no depth-stencil attachments yet");

    FramebufferKey key;
    UVec2 renderArea{};
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

    m_passFramebuffer = framebuffer(key);
    m_gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, m_passFramebuffer);
    // A pass covers its whole render area until the commands say otherwise.
    m_gl.Disable(GL_SCISSOR_TEST);
    m_gl.ViewportIndexedf(0, 0.0F, 0.0F, static_cast<f32>(renderArea.x), static_cast<f32>(renderArea.y));
    m_gl.DepthRangef(0.0F, 1.0F);

    // TODO(#34): restore color write masks before clearing once pipelines can change them
    std::array<GLenum, kMaxColorAttachments> undefined{};
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
        return std::ranges::any_of(framebuffer.key.color, [&](const FramebufferKey::Attachment& attachment) {
            return attachment.texture == handle;
        });
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
        const GlTexture& texture = m_textures.get(attachment.texture);
        if (attachment.arrayLayer == 0) {
            m_gl.NamedFramebufferTexture(name, point, texture.name, static_cast<GLint>(attachment.mipLevel));
        } else {
            m_gl.NamedFramebufferTextureLayer(name, point, texture.name,
                                              static_cast<GLint>(attachment.mipLevel),
                                              static_cast<GLint>(attachment.arrayLayer));
        }
        drawBuffers[static_cast<usize>(drawBufferCount++)] = point;
    }
    m_gl.NamedFramebufferDrawBuffers(name, drawBufferCount, drawBuffers.data());
    const GLenum status = m_gl.CheckNamedFramebufferStatus(name, GL_DRAW_FRAMEBUFFER);
    TESERRA_ASSERT(status == GL_FRAMEBUFFER_COMPLETE, "Framebuffer of texture {} is incomplete (0x{:X})",
                   key.color[0].texture, status);
    m_framebuffers.push_back({.key = key, .name = name});
    return name;
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
