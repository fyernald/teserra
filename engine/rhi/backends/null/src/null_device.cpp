#include <teserra/rhi_null/null_device.hpp>

#include <teserra/core/assert.hpp>
#include <teserra/platform/window.hpp>
#include <teserra/rhi/format.hpp>
#include <teserra/rhi/validation.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace teserra::rhi::null {

namespace {

Result<std::unique_ptr<Device>> createNullDevice(const DeviceDesc& desc, platform::Window* window) {
    return std::make_unique<NullDevice>(desc, window);
}

// The pointer behind a pool entry, or nullptr.
template <typename Pool, typename Handle>
auto* find(const Pool& pool, Handle handle) noexcept {
    const auto* object = pool.tryGet(handle);
    return object != nullptr ? &(*object)->desc : nullptr;
}

// The device's objects for validateCommands().
class NullLookup final : public ResourceLookup {
public:
    explicit NullLookup(const NullDevice& device)
        : m_device(device) {}

    [[nodiscard]] std::optional<BufferDesc> buffer(BufferHandle handle) const override {
        return copy(m_device.buffer(handle));
    }
    [[nodiscard]] std::optional<TextureDesc> texture(TextureHandle handle) const override {
        return copy(m_device.texture(handle));
    }
    [[nodiscard]] std::optional<SamplerDesc> sampler(SamplerHandle handle) const override {
        return copy(m_device.sampler(handle));
    }
    [[nodiscard]] std::optional<ShaderModuleInfo> shaderModule(ShaderModuleHandle handle) const override {
        const ShaderModuleDesc* desc = m_device.shaderModule(handle);
        if (desc == nullptr) {
            return std::nullopt;
        }
        return ShaderModuleInfo{.stage = desc->stage,
                                .reflection = m_device.shaderReflection(handle),
                                .debugName = desc->debugName};
    }
    [[nodiscard]] std::optional<BindGroupLayoutDesc>
    bindGroupLayout(BindGroupLayoutHandle handle) const override {
        return copy(m_device.bindGroupLayout(handle));
    }
    [[nodiscard]] std::optional<PipelineLayoutInfo>
    pipelineLayout(PipelineLayoutHandle handle) const override {
        return info(m_device.storedPipelineLayout(handle));
    }
    [[nodiscard]] std::optional<BindGroupInfo> bindGroup(BindGroupHandle handle) const override {
        return info(m_device.storedBindGroup(handle));
    }
    [[nodiscard]] std::optional<GraphicsPipelineDesc>
    graphicsPipeline(GraphicsPipelineHandle handle) const override {
        return copy(m_device.graphicsPipeline(handle));
    }
    [[nodiscard]] std::optional<PipelineLayoutInfo>
    graphicsPipelineLayout(GraphicsPipelineHandle handle) const override {
        return info(m_device.storedGraphicsPipelineLayout(handle));
    }

private:
    template <typename Desc>
    static std::optional<Desc> copy(const Desc* desc) {
        return desc != nullptr ? std::optional<Desc>(*desc) : std::nullopt;
    }
    template <typename Stored>
    static auto info(const Stored* stored) -> std::optional<decltype(stored->info())> {
        return stored != nullptr ? std::optional(stored->info()) : std::nullopt;
    }

    const NullDevice& m_device;
};

} // namespace

BackendFactory backendFactory() noexcept {
    return {.backend = Backend::Null, .create = &createNullDevice};
}

std::unique_ptr<NullDevice> createHeadlessDevice(const DeviceDesc& desc) {
    return std::make_unique<NullDevice>(desc, nullptr);
}

// ---------------------------------------------------------------------------------------------------------
// Swapchain

NullSwapchain::NullSwapchain(NullDevice& device, const SwapchainDesc& desc, UVec2 framebufferSize)
    : m_device(device), m_format(desc.format), m_size(framebufferSize) {
    createImage();
}

NullSwapchain::~NullSwapchain() {
    releaseImage();
}

void NullSwapchain::present() {
    TESERRA_ASSERT(!m_image.isNull(), "present() without an image; skip presenting while acquireNextImage() "
                                      "returns a null handle");
    if (!m_image.isNull()) {
        ++m_presentCount;
    }
}

void NullSwapchain::resize(UVec2 framebufferSize) {
    if (framebufferSize == m_size) {
        return;
    }
    releaseImage();
    m_size = framebufferSize;
    createImage();
}

void NullSwapchain::createImage() {
    if (m_size.x != 0 && m_size.y != 0) {
        m_image = m_device.createSwapchainImage(m_format, m_size);
    }
}

void NullSwapchain::releaseImage() {
    if (!m_image.isNull()) {
        m_device.destroy(m_image);
        m_image = {};
    }
}

// ---------------------------------------------------------------------------------------------------------
// Device

NullDevice::NullDevice(const DeviceDesc& desc, platform::Window* window)
    : m_validate(desc.enableValidation) {
    if (window != nullptr) {
        m_swapchain = std::make_unique<NullSwapchain>(*this, desc.swapchain, window->framebufferSize());
    }
}

NullDevice::~NullDevice() {
    m_swapchain.reset(); // its image is a texture of this device
}

template <typename Desc>
NullDevice::Object<Desc> NullDevice::store(const Desc& desc) {
    auto object = std::make_unique<Stored<Desc>>();
    object->desc = desc;
    object->debugName = desc.debugName;
    object->desc.debugName = object->debugName;
    if constexpr (std::is_same_v<Desc, ShaderModuleDesc>) {
        object->blob.assign(desc.blob.begin(), desc.blob.end());
        object->entryPoint = desc.entryPoint;
        object->desc.blob = object->blob;
        object->desc.entryPoint = object->entryPoint;
    } else if constexpr (std::is_same_v<Desc, BufferDesc>) {
        object->data.resize(desc.sizeInBytes);
    } else if constexpr (std::is_same_v<Desc, GraphicsPipelineDesc>) {
        object->vertexBuffers.assign(desc.vertexBuffers.begin(), desc.vertexBuffers.end());
        object->vertexAttributes.assign(desc.vertexAttributes.begin(), desc.vertexAttributes.end());
        object->desc.vertexBuffers = object->vertexBuffers;
        object->desc.vertexAttributes = object->vertexAttributes;
    }
    return object;
}

Result<BufferHandle> NullDevice::createBuffer(const BufferDesc& desc) {
    if (m_validate) {
        validateBufferDesc(desc);
    }
    return m_buffers.create(store(desc));
}

Result<TextureHandle> NullDevice::createTexture(const TextureDesc& desc) {
    if (m_validate) {
        validateTextureDesc(desc, m_limits);
    }
    return m_textures.create(store(desc));
}

Result<SamplerHandle> NullDevice::createSampler(const SamplerDesc& desc) {
    if (m_validate) {
        validateSamplerDesc(desc);
    }
    return m_samplers.create(store(desc));
}

Result<ShaderModuleHandle> NullDevice::createShaderModule(const ShaderModuleDesc& desc) {
    if (m_validate) {
        validateShaderModuleDesc(desc);
    }
    Object<ShaderModuleDesc> object = store(desc);
    if (Result<ShaderBlobContents> contents = readShaderBlob(desc.blob)) {
        object->reflection = std::move(contents->reflection);
    }
    return m_shaderModules.create(std::move(object));
}

Result<BindGroupLayoutHandle> NullDevice::createBindGroupLayout(const BindGroupLayoutDesc& desc) {
    if (m_validate) {
        validateBindGroupLayoutDesc(desc);
    }
    Object<BindGroupLayoutDesc> object = store(desc);
    object->layout = StoredBindGroupLayout::from(desc);
    object->desc.entries = object->layout.entries;
    return m_bindGroupLayouts.create(std::move(object));
}

Result<PipelineLayoutHandle> NullDevice::createPipelineLayout(const PipelineLayoutDesc& desc) {
    if (m_validate) {
        validatePipelineLayoutDesc(desc, NullLookup(*this));
    }
    Object<PipelineLayoutDesc> object = store(desc);
    std::array<const StoredBindGroupLayout*, kMaxBindGroups> layouts{};
    for (usize set = 0; set < desc.groups.size() && set < kMaxBindGroups; ++set) {
        const Object<BindGroupLayoutDesc>* layout = m_bindGroupLayouts.tryGet(desc.groups[set]);
        layouts[set] = layout != nullptr ? &(*layout)->layout : nullptr;
    }
    object->pipelineLayout = StoredPipelineLayout::from(desc.debugName, layouts);
    object->groupLayouts.assign(desc.groups.begin(), desc.groups.end());
    object->desc.groups = object->groupLayouts;
    return m_pipelineLayouts.create(std::move(object));
}

Result<BindGroupHandle> NullDevice::createBindGroup(const BindGroupDesc& desc) {
    if (m_validate) {
        validateBindGroupDesc(desc, NullLookup(*this));
    }
    Object<BindGroupDesc> object = store(desc);
    const Object<BindGroupLayoutDesc>* layout = m_bindGroupLayouts.tryGet(desc.layout);
    object->group = StoredBindGroup::from(desc, layout != nullptr ? &(*layout)->layout : nullptr);
    object->desc.entries = object->group.entries;
    return m_bindGroups.create(std::move(object));
}

Result<GraphicsPipelineHandle> NullDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    if (m_validate) {
        validateGraphicsPipelineDesc(desc);
        TESERRA_ASSERT(m_shaderModules.contains(desc.vertexShader),
                       "Graphics pipeline '{}': vertex shader {} is not alive", desc.debugName,
                       desc.vertexShader);
        TESERRA_ASSERT(desc.fragmentShader.isNull() || m_shaderModules.contains(desc.fragmentShader),
                       "Graphics pipeline '{}': fragment shader {} is not alive", desc.debugName,
                       desc.fragmentShader);
        validateGraphicsPipelineResources(desc, NullLookup(*this));
    }
    Object<GraphicsPipelineDesc> object = store(desc);
    if (const StoredPipelineLayout* layout = storedPipelineLayout(desc.layout)) {
        object->pipelineLayout = *layout;
    }
    return m_pipelines.create(std::move(object));
}

TextureHandle NullDevice::createSwapchainImage(Format format, UVec2 size) {
    return m_textures.create(store(TextureDesc{
        .format = format,
        .width = size.x,
        .height = size.y,
        .usage = TextureUsage::RenderTarget | TextureUsage::CopySrc,
        .debugName = "Swapchain image",
    }));
}

void NullDevice::destroy(BufferHandle handle) {
    if (!handle.isNull()) {
        m_buffers.destroy(handle);
    }
}

void NullDevice::destroy(TextureHandle handle) {
    if (!handle.isNull()) {
        m_textures.destroy(handle);
    }
}

void NullDevice::destroy(SamplerHandle handle) {
    if (!handle.isNull()) {
        m_samplers.destroy(handle);
    }
}

void NullDevice::destroy(ShaderModuleHandle handle) {
    if (!handle.isNull()) {
        m_shaderModules.destroy(handle);
    }
}

void NullDevice::destroy(BindGroupLayoutHandle handle) {
    if (!handle.isNull()) {
        m_bindGroupLayouts.destroy(handle);
    }
}

void NullDevice::destroy(PipelineLayoutHandle handle) {
    if (!handle.isNull()) {
        m_pipelineLayouts.destroy(handle);
    }
}

void NullDevice::destroy(BindGroupHandle handle) {
    if (!handle.isNull()) {
        m_bindGroups.destroy(handle);
    }
}

void NullDevice::destroy(GraphicsPipelineHandle handle) {
    if (!handle.isNull()) {
        m_pipelines.destroy(handle);
    }
}

void NullDevice::writeBuffer(BufferHandle buffer, u64 offset, std::span<const std::byte> data) {
    Object<BufferDesc>* object = m_buffers.tryGet(buffer);
    TESERRA_ASSERT(object != nullptr, "writeBuffer to buffer {}, which is not alive", buffer);
    if (object == nullptr) {
        return;
    }
    if (m_validate) {
        validateWriteBuffer((*object)->desc, offset, data.size());
    }
    if (!data.empty() && offset <= (*object)->data.size() && data.size() <= (*object)->data.size() - offset) {
        std::ranges::copy(data, (*object)->data.begin() + static_cast<std::ptrdiff_t>(offset));
    }
}

std::span<std::byte> NullDevice::map(BufferHandle buffer) {
    Object<BufferDesc>* object = m_buffers.tryGet(buffer);
    TESERRA_ASSERT(object != nullptr, "map() of buffer {}, which is not alive", buffer);
    if (object == nullptr) {
        return {};
    }
    if (m_validate) {
        validateMapBuffer((*object)->desc);
    }
    return (*object)->data;
}

void NullDevice::beginFrame() {
    TESERRA_ASSERT(!m_inFrame, "beginFrame() twice without endFrame()");
    for (CommandList& list : m_commandLists[m_frame % kMaxFramesInFlight]) {
        list.reset();
    }
    m_commandListsInUse = 0;
    m_submitted.clear();
    m_inFrame = true;
}

void NullDevice::endFrame() {
    TESERRA_ASSERT(m_inFrame, "endFrame() without beginFrame()");
    m_inFrame = false;
    ++m_frame;
}

CommandList& NullDevice::acquireCommandList() {
    TESERRA_ASSERT(m_inFrame, "acquireCommandList() outside beginFrame() / endFrame()");
    std::deque<CommandList>& lists = m_commandLists[m_frame % kMaxFramesInFlight];
    if (m_commandListsInUse == lists.size()) {
        lists.emplace_back();
    }
    return lists[m_commandListsInUse++];
}

void NullDevice::submit(CommandList& commands) {
    commands.finish();
    if (m_validate) {
        validateCommands(commands, NullLookup(*this));
    }
    m_submitted.push_back(&commands);
}

usize NullDevice::liveObjectCount() const noexcept {
    usize count = 0;
    const auto add = [&](auto /*handle*/, const auto& /*object*/) { ++count; };
    m_buffers.forEach(add);
    m_textures.forEach(add);
    m_samplers.forEach(add);
    m_shaderModules.forEach(add);
    m_bindGroupLayouts.forEach(add);
    m_pipelineLayouts.forEach(add);
    m_bindGroups.forEach(add);
    m_pipelines.forEach(add);
    return count;
}

const BufferDesc* NullDevice::buffer(BufferHandle handle) const noexcept {
    return find(m_buffers, handle);
}

const TextureDesc* NullDevice::texture(TextureHandle handle) const noexcept {
    return find(m_textures, handle);
}

const SamplerDesc* NullDevice::sampler(SamplerHandle handle) const noexcept {
    return find(m_samplers, handle);
}

const ShaderModuleDesc* NullDevice::shaderModule(ShaderModuleHandle handle) const noexcept {
    return find(m_shaderModules, handle);
}

const BindGroupLayoutDesc* NullDevice::bindGroupLayout(BindGroupLayoutHandle handle) const noexcept {
    return find(m_bindGroupLayouts, handle);
}

const PipelineLayoutDesc* NullDevice::pipelineLayout(PipelineLayoutHandle handle) const noexcept {
    return find(m_pipelineLayouts, handle);
}

const BindGroupDesc* NullDevice::bindGroup(BindGroupHandle handle) const noexcept {
    return find(m_bindGroups, handle);
}

const GraphicsPipelineDesc* NullDevice::graphicsPipeline(GraphicsPipelineHandle handle) const noexcept {
    return find(m_pipelines, handle);
}

const ShaderReflection* NullDevice::shaderReflection(ShaderModuleHandle handle) const noexcept {
    const Object<ShaderModuleDesc>* object = m_shaderModules.tryGet(handle);
    return object != nullptr && (*object)->reflection ? &*(*object)->reflection : nullptr;
}

const StoredPipelineLayout* NullDevice::storedPipelineLayout(PipelineLayoutHandle handle) const noexcept {
    const Object<PipelineLayoutDesc>* object = m_pipelineLayouts.tryGet(handle);
    return object != nullptr ? &(*object)->pipelineLayout : nullptr;
}

const StoredBindGroup* NullDevice::storedBindGroup(BindGroupHandle handle) const noexcept {
    const Object<BindGroupDesc>* object = m_bindGroups.tryGet(handle);
    return object != nullptr ? &(*object)->group : nullptr;
}

const StoredPipelineLayout*
NullDevice::storedGraphicsPipelineLayout(GraphicsPipelineHandle handle) const noexcept {
    const Object<GraphicsPipelineDesc>* object = m_pipelines.tryGet(handle);
    return object != nullptr ? &(*object)->pipelineLayout : nullptr;
}

std::span<const std::byte> NullDevice::bufferData(BufferHandle handle) const noexcept {
    const Object<BufferDesc>* object = m_buffers.tryGet(handle);
    return object != nullptr ? std::span<const std::byte>((*object)->data) : std::span<const std::byte>();
}

} // namespace teserra::rhi::null
