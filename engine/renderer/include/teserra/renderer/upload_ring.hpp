#pragma once

// Per-frame upload memory (docs/architecture/renderer.md#frame-structure): one persistently mapped buffer
// split into a partition per frame slot. A frame writes only its slot's partition, which the GPU no longer
// reads once rhi::Device::beginFrame() has returned, so the CPU never overwrites data still in flight.

#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/handles.hpp>

#include <cstddef>
#include <optional>
#include <span>

namespace teserra::renderer {

/// A ring of upload memory with one partition per frame in flight. Vertex, index and uniform data of a
/// frame are sub-allocated from it and bound by buffer and offset.
///
///     ring.beginFrame();                                   // after device.beginFrame()
///     auto camera = ring.allocate(sizeof(CameraData), device.limits().uniformBufferOffsetAlignment);
///     std::memcpy(camera->memory.data(), &data, sizeof(data));
///     // bind camera->buffer at camera->offset
class UploadRing {
public:
    /// A sub-allocation of the current frame.
    struct Allocation {
        rhi::BufferHandle buffer{};
        /// From the start of the buffer, for binding.
        u64 offset = 0;
        /// The mapped bytes to write.
        std::span<std::byte> memory{};
    };

    /// Creates the buffer: kMaxFramesInFlight partitions of `bytesPerFrame` bytes, rounded up to 256 so
    /// that every partition starts at any uniform buffer offset alignment a device may have. Fails if the
    /// buffer cannot be created, or with InvalidArgument if the ring would have 4 GiB or more (offsets are
    /// bound as 32-bit dynamic offsets).
    [[nodiscard]] static Result<UploadRing> create(rhi::Device& device, u64 bytesPerFrame);

    UploadRing(UploadRing&& other) noexcept;
    UploadRing& operator=(UploadRing&& other) noexcept;
    UploadRing(const UploadRing&) = delete;
    UploadRing& operator=(const UploadRing&) = delete;
    ~UploadRing();

    /// Starts the device's current frame: empties the partition of rhi::Device::frameSlot(). Call it after
    /// the device's beginFrame(), once per frame.
    void beginFrame();

    /// `size` bytes at a multiple of `alignment` (a power of two) in the current frame's partition. Asserts
    /// and returns nothing if the partition is full, or if the device has moved to another frame since
    /// beginFrame(): writing then could overwrite data that frame's GPU work still reads.
    [[nodiscard]] std::optional<Allocation> allocate(u64 size, u64 alignment);

    /// The ring's buffer, for bind groups that bind it with dynamic offsets.
    [[nodiscard]] rhi::BufferHandle buffer() const noexcept { return m_buffer; }
    /// The size of each frame's partition.
    [[nodiscard]] u64 bytesPerFrame() const noexcept { return m_bytesPerFrame; }
    /// Bytes allocated in the current frame, alignment padding included.
    [[nodiscard]] u64 usedBytes() const noexcept { return m_used; }

private:
    UploadRing(rhi::Device& device, rhi::BufferHandle buffer, u64 bytesPerFrame);

    rhi::Device* m_device = nullptr;
    rhi::BufferHandle m_buffer{};
    u64 m_bytesPerFrame = 0;
    std::span<std::byte> m_memory{};
    // The slot of the frame begun last; none before the first beginFrame().
    std::optional<u32> m_slot{};
    u64 m_used = 0;
};

} // namespace teserra::renderer
