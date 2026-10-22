#include "gl_device.hpp"

#include <teserra/core/assert.hpp>
#include <teserra/core/log.hpp>
#include <teserra/platform/opengl_context.hpp>
#include <teserra/platform/window.hpp>

#include "gl_debug.hpp"

#include <string_view>

namespace teserra::rhi::opengl {

namespace {

GLADapiproc loadFunction(void* context, const char* name) {
    return static_cast<platform::OpenGlContext*>(context)->getProcAddress(name);
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
    TESERRA_TRY(device->initialize(desc));
    return device;
}

GlDevice::GlDevice(platform::OpenGlContext& context)
    : m_context(context), m_thread(std::this_thread::get_id()) {
}

Result<void> GlDevice::initialize(const DeviceDesc& desc) {
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

    if (desc.enableBackendDebug) {
        // Synchronous output logs a message while the call that caused it is still on the stack.
        m_gl.Enable(GL_DEBUG_OUTPUT);
        m_gl.Enable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        m_gl.DebugMessageCallback(&logDebugMessage, nullptr);
    }
    // The RHI's clip-space and framebuffer conventions (ADR-0024): depth [0, 1], row 0 at the top.
    m_gl.ClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);

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
// Resources: created by the OpenGL backend from iteration 5 on.

Result<BufferHandle> GlDevice::createBuffer(const BufferDesc& desc) {
    // TODO(#32): buffers
    return fail(ErrorCode::Unsupported, Module::Rhi, "The OpenGL backend cannot create buffers yet ('{}')",
                desc.debugName);
}

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

// Nothing can be created yet, so only null handles can be destroyed.
void GlDevice::destroy(BufferHandle handle) {
    TESERRA_ASSERT(handle.isNull(), "Destroying buffer {}, which this device did not create", handle);
}

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
    // TODO(#28): replay the commands; until then, none may be recorded, so none are dropped silently
    TESERRA_ASSERT(commands.empty(), "The OpenGL backend cannot replay commands yet: {}",
                   commandTypeName((*commands.commands().begin()).type()));
}

Swapchain* GlDevice::swapchain() noexcept {
    return nullptr; // TODO(#28): the swapchain of the window
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
