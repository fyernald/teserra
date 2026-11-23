#include <teserra/renderer/upload_ring.hpp>

#include <teserra/core/assert.hpp>
#include <teserra/rhi/buffer.hpp>
#include <teserra/rhi/limits.hpp>

#include <bit>
#include <limits>
#include <utility>

namespace teserra::renderer {

namespace {

// Every partition starts at a multiple of this, the largest uniform buffer offset alignment a device may
// report (DeviceLimits::uniformBufferOffsetAlignment).
constexpr u64 kPartitionAlignment = 256;

constexpr u64 alignUp(u64 value, u64 alignment) noexcept {
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace

Result<UploadRing> UploadRing::create(rhi::Device& device, u64 bytesPerFrame) {
    const u64 partition = alignUp(bytesPerFrame, kPartitionAlignment);
    const u64 size = partition * rhi::kMaxFramesInFlight;
    if (bytesPerFrame == 0 || size > std::numeric_limits<u32>::max()) {
        return fail(
            ErrorCode::InvalidArgument, Module::Renderer,
            "Upload ring of {} bytes per frame: it needs more than 0 bytes and less than 4 GiB in all",
            bytesPerFrame);
    }
    TESERRA_TRY_ASSIGN(const rhi::BufferHandle buffer,
                       device.createBuffer({
                           .sizeInBytes = size,
                           .usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::Vertex |
                                    rhi::BufferUsage::Index | rhi::BufferUsage::CopySrc,
                           .memory = rhi::MemoryLocation::Upload,
                           .debugName = "Upload ring",
                       }));
    return UploadRing(device, buffer, partition);
}

UploadRing::UploadRing(rhi::Device& device, rhi::BufferHandle buffer, u64 bytesPerFrame)
    : m_device(&device), m_buffer(buffer), m_bytesPerFrame(bytesPerFrame), m_memory(device.map(buffer)) {
}

UploadRing::UploadRing(UploadRing&& other) noexcept
    : m_device(other.m_device),
      m_buffer(std::exchange(other.m_buffer, {})),
      m_bytesPerFrame(other.m_bytesPerFrame),
      m_memory(std::exchange(other.m_memory, {})),
      m_slot(std::exchange(other.m_slot, std::nullopt)),
      m_used(std::exchange(other.m_used, 0)) {
}

UploadRing& UploadRing::operator=(UploadRing&& other) noexcept {
    if (this != &other) {
        if (m_device != nullptr) {
            m_device->destroy(m_buffer);
        }
        m_device = other.m_device;
        m_buffer = std::exchange(other.m_buffer, {});
        m_bytesPerFrame = other.m_bytesPerFrame;
        m_memory = std::exchange(other.m_memory, {});
        m_slot = std::exchange(other.m_slot, std::nullopt);
        m_used = std::exchange(other.m_used, 0);
    }
    return *this;
}

UploadRing::~UploadRing() {
    if (m_device != nullptr) {
        m_device->destroy(m_buffer); // a moved-from ring holds the null handle, which is ignored
    }
}

// SCRATCH (#204, never merged): `slot` appears only in the assertion, so a Release build, where assertions
// are off, warns about an unused parameter.
static void scratchCheckSlot(u32 slot) {
    TESERRA_ASSERT(slot < rhi::kMaxFramesInFlight, "frame slot {} out of range", slot);
}

void UploadRing::beginFrame() {
    scratchCheckSlot(m_device->frameSlot());
    m_slot = m_device->frameSlot();
    m_used = 0;
}

std::optional<UploadRing::Allocation> UploadRing::allocate(u64 size, u64 alignment) {
    TESERRA_ASSERT(std::has_single_bit(alignment), "UploadRing::allocate: alignment {} is not a power of two",
                   alignment);
    TESERRA_ASSERT(m_slot.has_value(), "UploadRing::allocate before the first beginFrame()");
    if (!m_slot) {
        return std::nullopt;
    }
    const u32 deviceSlot = m_device->frameSlot();
    TESERRA_ASSERT(
        *m_slot == deviceSlot,
        "UploadRing::allocate in frame slot {}, but beginFrame() was called for slot {}: the slot's "
        "data could still be in flight",
        deviceSlot, *m_slot);
    if (*m_slot != deviceSlot) {
        return std::nullopt;
    }
    const u64 offset = alignUp(m_used, alignment);
    const bool fits = offset <= m_bytesPerFrame && size <= m_bytesPerFrame - offset;
    TESERRA_ASSERT(fits, "UploadRing::allocate of {} bytes: the frame's {} bytes are full ({} used)", size,
                   m_bytesPerFrame, m_used);
    if (!fits) {
        return std::nullopt;
    }
    m_used = offset + size;
    const u64 start = (u64{*m_slot} * m_bytesPerFrame) + offset;
    return Allocation{
        .buffer = m_buffer,
        .offset = start,
        .memory = m_memory.subspan(static_cast<usize>(start), static_cast<usize>(size)),
    };
}

} // namespace teserra::renderer
