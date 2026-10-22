#pragma once

// The data of command recording (docs/architecture/rhi.md#command-recording-and-submission): the argument
// structs of CommandList, and the records it appends to its byte stream, which backends read back through
// Command.
//
// Stream layout (spike #15): every record starts on an 8-byte boundary with a CommandHeader, followed by
// its payload (one of the *Command structs below, none for EndRenderPass and PopDebugGroup) and, for some
// commands, a trailing array at the next 8-byte boundary. CommandHeader::size covers all of it, padding
// included, so a reader can skip a record without knowing its type. Payloads are trivially copyable and
// written with memcpy; readers use them in place.

#include <teserra/core/assert.hpp>
#include <teserra/core/types.hpp>
#include <teserra/rhi/format.hpp>
#include <teserra/rhi/handles.hpp>
#include <teserra/rhi/render_pass.hpp>

#include <concepts>
#include <cstddef>
#include <iterator>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>

namespace teserra::rhi {

// ---------------------------------------------------------------------------------------------------------
// Arguments

/// A vertex buffer bound to a slot, read from `offset` bytes on.
struct VertexBufferBinding {
    BufferHandle buffer{};
    u64 offset = 0;
};

/// The area of the render targets that draws map to, in pixels from the top-left corner, and the depth
/// range they map to.
struct Viewport {
    f32 x = 0.0F;
    f32 y = 0.0F;
    f32 width = 0.0F;
    f32 height = 0.0F;
    f32 minDepth = 0.0F;
    f32 maxDepth = 1.0F;
};

/// The area of the render targets that draws may write, in pixels from the top-left corner.
struct ScissorRect {
    u32 x = 0;
    u32 y = 0;
    u32 width = 0;
    u32 height = 0;
};

/// A non-indexed draw.
struct DrawArgs {
    u32 vertexCount = 0;
    u32 instanceCount = 1;
    u32 firstVertex = 0;
    u32 firstInstance = 0;
};

/// An indexed draw, with indices from the bound index buffer.
struct DrawIndexedArgs {
    u32 indexCount = 0;
    u32 instanceCount = 1;
    u32 firstIndex = 0;
    /// Added to each index before it selects a vertex.
    i32 vertexOffset = 0;
    u32 firstInstance = 0;
};

/// A copy of `size` bytes between buffers.
struct BufferCopy {
    BufferHandle source{};
    u64 sourceOffset = 0;
    BufferHandle destination{};
    u64 destinationOffset = 0;
    u64 size = 0;
};

/// A copy between texel data in a buffer and a region of a texture, in either direction. Rows are in
/// top-to-bottom order in both (docs/architecture/rhi.md#conventions); the layers of a multi-layer copy
/// follow each other in the buffer.
struct BufferTextureCopy {
    BufferHandle buffer{};
    u64 bufferOffset = 0;
    /// Bytes from the start of one texel row to the next in the buffer; 0 means tightly packed.
    u32 bytesPerRow = 0;
    TextureHandle texture{};
    u32 mipLevel = 0;
    /// First array layer, cube face or depth slice.
    u32 arrayLayer = 0;
    u32 layerCount = 1;
    /// Top-left texel of the region.
    u32 x = 0;
    u32 y = 0;
    u32 width = 0;
    u32 height = 0;
};

// ---------------------------------------------------------------------------------------------------------
// Records

/// The command a record holds.
// NOLINTNEXTLINE(performance-enum-size): the stream layout of spike #15 has a u16 type in the header
enum class CommandType : u16 {
    BeginRenderPass,
    EndRenderPass,
    BindPipeline,
    BindGroup,
    BindVertexBuffers,
    BindIndexBuffer,
    SetViewport,
    SetScissor,
    Draw,
    DrawIndexed,
    CopyBuffer,
    CopyBufferToTexture,
    CopyTextureToBuffer,
    PushDebugGroup,
    PopDebugGroup,
};

/// Number of CommandType values.
inline constexpr usize kCommandTypeCount = 15;

/// Name of a command type, e.g. "BeginRenderPass".
[[nodiscard]] std::string_view commandTypeName(CommandType type) noexcept;

/// Alignment of records and of their trailing arrays.
inline constexpr usize kCommandAlignment = 8;

/// Start of every record.
struct CommandHeader {
    CommandType type = CommandType::EndRenderPass;
    u16 reserved = 0;
    /// Bytes of the whole record: header, payload, trailing array and padding.
    u32 size = 0;
};
static_assert(sizeof(CommandHeader) == kCommandAlignment);

struct BeginRenderPassCommand {
    static constexpr CommandType kType = CommandType::BeginRenderPass;
    RenderPassDesc pass;
};

struct BindPipelineCommand {
    static constexpr CommandType kType = CommandType::BindPipeline;
    GraphicsPipelineHandle pipeline;
};

/// Followed by `dynamicOffsetCount` u32 dynamic offsets.
struct BindGroupCommand {
    static constexpr CommandType kType = CommandType::BindGroup;
    u32 index;
    BindGroupHandle group;
    u32 dynamicOffsetCount;
};

/// Followed by `count` VertexBufferBindings for the slots from `firstSlot` on.
struct BindVertexBuffersCommand {
    static constexpr CommandType kType = CommandType::BindVertexBuffers;
    u32 firstSlot;
    u32 count;
};

struct BindIndexBufferCommand {
    static constexpr CommandType kType = CommandType::BindIndexBuffer;
    BufferHandle buffer;
    IndexType indexType;
    u64 offset;
};

struct SetViewportCommand {
    static constexpr CommandType kType = CommandType::SetViewport;
    Viewport viewport;
};

struct SetScissorCommand {
    static constexpr CommandType kType = CommandType::SetScissor;
    ScissorRect scissor;
};

struct DrawCommand {
    static constexpr CommandType kType = CommandType::Draw;
    DrawArgs args;
};

struct DrawIndexedCommand {
    static constexpr CommandType kType = CommandType::DrawIndexed;
    DrawIndexedArgs args;
};

struct CopyBufferCommand {
    static constexpr CommandType kType = CommandType::CopyBuffer;
    BufferCopy copy;
};

struct CopyBufferToTextureCommand {
    static constexpr CommandType kType = CommandType::CopyBufferToTexture;
    BufferTextureCopy copy;
};

struct CopyTextureToBufferCommand {
    static constexpr CommandType kType = CommandType::CopyTextureToBuffer;
    BufferTextureCopy copy;
};

/// Followed by the `labelLength` characters of the label, not null-terminated.
struct PushDebugGroupCommand {
    static constexpr CommandType kType = CommandType::PushDebugGroup;
    u32 labelLength;
};

/// A record payload: trivially copyable, no stricter alignment than the records, and naming its type.
template <typename T>
concept CommandPayload = std::is_trivially_copyable_v<T> && alignof(T) <= kCommandAlignment && requires {
    { T::kType } -> std::convertible_to<CommandType>;
};

/// Offset of the trailing array in a record whose payload has `payloadSize` bytes.
[[nodiscard]] constexpr usize commandTrailingOffset(usize payloadSize) noexcept {
    return (sizeof(CommandHeader) + payloadSize + kCommandAlignment - 1) / kCommandAlignment *
           kCommandAlignment;
}

// ---------------------------------------------------------------------------------------------------------
// Reading

/// One record of a command stream, as backends read it: a view into the CommandList, valid while the list
/// is neither recorded into nor reset. Payloads and trailing arrays are read in place.
///
///     for (const Command command : list.commands()) {
///         switch (command.type()) {
///         case CommandType::Draw:
///             draw(command.as<DrawCommand>().args);
///             break;
///         case CommandType::BindVertexBuffers:
///             bindVertexBuffers(command.as<BindVertexBuffersCommand>().firstSlot, command.vertexBuffers());
///             break;
///         ...
class Command {
public:
    explicit Command(const std::byte* record) noexcept
        : m_record(record) {}

    [[nodiscard]] CommandType type() const noexcept { return at<CommandHeader>(0)->type; }
    /// Bytes of the record, padding included.
    [[nodiscard]] u32 size() const noexcept { return at<CommandHeader>(0)->size; }

    /// The payload. `T` must be the payload of this command's type.
    template <CommandPayload T>
    [[nodiscard]] const T& as() const {
        TESERRA_ASSERT(type() == T::kType, "Reading a {} command as {}", commandTypeName(type()),
                       commandTypeName(T::kType));
        return *at<T>(sizeof(CommandHeader));
    }

    /// The dynamic offsets of a BindGroup command.
    [[nodiscard]] std::span<const u32> dynamicOffsets() const {
        return trailing<BindGroupCommand, u32>(as<BindGroupCommand>().dynamicOffsetCount);
    }

    /// The bindings of a BindVertexBuffers command, for consecutive slots from its firstSlot on.
    [[nodiscard]] std::span<const VertexBufferBinding> vertexBuffers() const {
        return trailing<BindVertexBuffersCommand, VertexBufferBinding>(as<BindVertexBuffersCommand>().count);
    }

    /// The label of a PushDebugGroup command.
    [[nodiscard]] std::string_view debugLabel() const {
        const std::span<const char> label =
            trailing<PushDebugGroupCommand, char>(as<PushDebugGroupCommand>().labelLength);
        return {label.data(), label.size()};
    }

private:
    // The objects in the stream were created implicitly by memcpy into suitably aligned byte storage.
    template <typename T>
    [[nodiscard]] const T* at(usize offset) const noexcept {
        return std::launder(reinterpret_cast<const T*>(m_record + offset));
    }

    template <CommandPayload Payload, typename T>
    [[nodiscard]] std::span<const T> trailing(u32 count) const {
        constexpr usize kOffset = commandTrailingOffset(sizeof(Payload));
        TESERRA_ASSERT(kOffset + (usize{count} * sizeof(T)) <= size(),
                       "{} elements do not fit in the trailing array of a {} command", count,
                       commandTypeName(type()));
        return {at<T>(kOffset), count};
    }

    const std::byte* m_record;
};

/// Forward iteration over the records of a command stream.
class CommandIterator {
public:
    // A forward iterator whose operator* returns a Command by value, so for pre-C++20 algorithms it is an
    // input iterator (like the iterators of std::views::iota). The names are those std::iterator_traits uses.
    // NOLINTBEGIN(readability-identifier-naming)
    using iterator_concept = std::forward_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = Command;
    using difference_type = std::ptrdiff_t;
    using reference = Command;
    using pointer = void;
    // NOLINTEND(readability-identifier-naming)

    CommandIterator() = default;
    explicit CommandIterator(const std::byte* record) noexcept
        : m_record(record) {}

    [[nodiscard]] Command operator*() const noexcept { return Command(m_record); }

    CommandIterator& operator++() noexcept {
        m_record += Command(m_record).size();
        return *this;
    }
    CommandIterator operator++(int) noexcept {
        CommandIterator previous = *this;
        ++*this;
        return previous;
    }

    friend bool operator==(CommandIterator, CommandIterator) noexcept = default;

private:
    const std::byte* m_record = nullptr;
};

/// The records of a command stream, in recording order.
class CommandRange {
public:
    CommandRange() = default;
    explicit CommandRange(std::span<const std::byte> bytes) noexcept
        : m_bytes(bytes) {}

    [[nodiscard]] CommandIterator begin() const noexcept { return CommandIterator(m_bytes.data()); }
    [[nodiscard]] CommandIterator end() const noexcept {
        return CommandIterator(m_bytes.data() + m_bytes.size());
    }

private:
    std::span<const std::byte> m_bytes;
};

} // namespace teserra::rhi
