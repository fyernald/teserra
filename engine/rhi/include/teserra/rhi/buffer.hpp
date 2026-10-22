#pragma once

#include <teserra/core/flags.hpp>
#include <teserra/core/types.hpp>

#include <string_view>

namespace teserra::rhi {

/// How a buffer may be used. A buffer is created with every usage it will ever have.
enum class BufferUsage : u8 {
    None = 0,
    Vertex = 1U << 0U,
    Index = 1U << 1U,
    Uniform = 1U << 2U,
    /// Read-only storage buffer in shaders (v1).
    Storage = 1U << 3U,
    /// Source of copy commands.
    CopySrc = 1U << 4U,
    /// Destination of copy commands and Device uploads.
    CopyDst = 1U << 5U,
};
TESERRA_FLAGS(BufferUsage)

/// Where a buffer's memory lives, which decides how the CPU may access it.
enum class MemoryLocation : u8 {
    /// Device memory; written through uploads and copies.
    GpuOnly,
    /// CPU writes, GPU reads: per-frame data, streamed vertices. May be persistently mapped.
    Upload,
    /// GPU writes, CPU reads: readback of rendering results.
    Readback,
};

/// Creation parameters of a buffer.
struct BufferDesc {
    u64 sizeInBytes = 0;
    BufferUsage usage = BufferUsage::None;
    MemoryLocation memory = MemoryLocation::GpuOnly;
    /// Shown by graphics debuggers and in validation messages.
    std::string_view debugName{};
};

} // namespace teserra::rhi
