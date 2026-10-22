// Spike #15: validate the command-stream recording model (docs/architecture/rhi.md#command-recording-and-submission).
// Spike code: never merged (docs/process/development-process.md#spikes).
//
// Records a representative frame of ~10k commands into a byte stream and replays it into a mock backend
// that counts calls, in four variants:
//   packed   {u16 type, u16 size} header, payload right after it (4-byte steps), read back with memcpy
//   aligned  same header, but every record starts on an 8-byte boundary; payloads read in place
//   variant  std::vector<std::variant<...>>, the "obvious" alternative (variable-size data in side vectors)
//   direct   no recording: virtual calls straight into the backend, as a lower bound
// Variable-size payloads: bindVertexBuffers (N {buffer, offset} pairs), bindGroup (N dynamic offsets),
// pushDebugGroup (string). Heap allocations are counted after warm-up.

#include <teserra/core/handle.hpp>
#include <teserra/core/types.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

using namespace teserra;

// ---------------------------------------------------------------------------------------------------
// Allocation counting

namespace {
std::atomic<u64> g_allocations{0};
}

void* operator new(std::size_t size) {
    ++g_allocations;
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        return p;
    }
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}

namespace {

// ---------------------------------------------------------------------------------------------------
// Commands (POD payloads)

using BufferHandle = Handle<struct BufferTag>;
using TextureHandle = Handle<struct TextureTag>;
using PipelineHandle = Handle<struct PipelineTag>;
using BindGroupHandle = Handle<struct BindGroupTag>;

enum class CommandType : u16 {
    BeginRenderPass,
    EndRenderPass,
    BindPipeline,
    BindGroup,         // + u32 dynamicOffsets[offsetCount]
    BindVertexBuffers, // + VertexBufferBinding[count]
    BindIndexBuffer,
    SetViewport,
    Draw,
    DrawIndexed,
    PushDebugGroup, // + char[length]
    PopDebugGroup,
    Count,
};

struct CommandHeader {
    CommandType type;
    u16 size; // header + payload + trailing data (+ padding in the aligned variant)
};
static_assert(sizeof(CommandHeader) == 4);

struct ColorAttachment {
    TextureHandle texture;
    u32 loadOp;
    u32 storeOp;
    std::array<f32, 4> clearColor;
};
struct BeginRenderPassCmd {
    std::array<ColorAttachment, 4> color;
    u32 colorCount;
    TextureHandle depth;
    f32 clearDepth;
};
struct EndRenderPassCmd {};
struct BindPipelineCmd {
    PipelineHandle pipeline;
};
struct BindGroupCmd {
    u32 index;
    BindGroupHandle group;
    u32 offsetCount;
};
struct VertexBufferBinding {
    BufferHandle buffer;
    u64 offset;
};
struct BindVertexBuffersCmd {
    u32 firstSlot;
    u32 count;
};
struct BindIndexBufferCmd {
    BufferHandle buffer;
    u64 offset;
    u32 indexType;
};
struct SetViewportCmd {
    f32 x, y, width, height, minDepth, maxDepth;
};
struct DrawCmd {
    u32 vertexCount, instanceCount, firstVertex, firstInstance;
};
struct DrawIndexedCmd {
    u32 indexCount, instanceCount, firstIndex;
    i32 vertexOffset;
    u32 firstInstance;
};
struct PushDebugGroupCmd {
    u32 length;
};
struct PopDebugGroupCmd {};

// ---------------------------------------------------------------------------------------------------
// Mock backend: counts calls and folds payload values into a checksum so nothing is optimized away.

struct MockBackend {
    std::array<u64, static_cast<usize>(CommandType::Count)> calls{};
    u64 checksum = 0;

    void count(CommandType type, u64 value) {
        ++calls[static_cast<usize>(type)];
        checksum = checksum * 31 + value;
    }
};

// The same calls as virtual functions, for the "direct" variant.
class Backend {
public:
    virtual ~Backend() = default;
    virtual void beginRenderPass(const BeginRenderPassCmd& c) = 0;
    virtual void endRenderPass() = 0;
    virtual void bindPipeline(PipelineHandle p) = 0;
    virtual void bindGroup(u32 index, BindGroupHandle g, std::span<const u32> offsets) = 0;
    virtual void bindVertexBuffers(u32 first, std::span<const VertexBufferBinding> b) = 0;
    virtual void setViewport(const SetViewportCmd& c) = 0;
    virtual void draw(const DrawCmd& c) = 0;
    virtual void pushDebugGroup(std::string_view name) = 0;
    virtual void popDebugGroup() = 0;
};

class MockVirtualBackend final : public Backend {
public:
    MockBackend counter;
    void beginRenderPass(const BeginRenderPassCmd& c) override {
        counter.count(CommandType::BeginRenderPass, c.colorCount);
    }
    void endRenderPass() override { counter.count(CommandType::EndRenderPass, 0); }
    void bindPipeline(PipelineHandle p) override { counter.count(CommandType::BindPipeline, p.index()); }
    void bindGroup(u32 index, BindGroupHandle g, std::span<const u32> offsets) override {
        counter.count(CommandType::BindGroup, index + g.index() + (offsets.empty() ? 0 : offsets[0]));
    }
    void bindVertexBuffers(u32 first, std::span<const VertexBufferBinding> b) override {
        counter.count(CommandType::BindVertexBuffers, first + b.size() + b[0].offset);
    }
    void setViewport(const SetViewportCmd& c) override {
        counter.count(CommandType::SetViewport, static_cast<u64>(c.width));
    }
    void draw(const DrawCmd& c) override { counter.count(CommandType::Draw, c.vertexCount + c.firstVertex); }
    void pushDebugGroup(std::string_view name) override { counter.count(CommandType::PushDebugGroup, name.size()); }
    void popDebugGroup() override { counter.count(CommandType::PopDebugGroup, 0); }
};

// ---------------------------------------------------------------------------------------------------
// Byte stream, parameterized by record alignment (4 = packed, 8 = aligned)

template <usize RecordAlignment>
class CommandStream {
public:
    static constexpr usize alignUp(usize value) { return (value + RecordAlignment - 1) & ~(RecordAlignment - 1); }
    // Payload offset inside a record: right after the header when packed; on an 8-byte boundary when
    // aligned (payloads contain u64 offsets).
    static constexpr usize kPayloadOffset = RecordAlignment == 4 ? sizeof(CommandHeader) : 8;

    void reset() { m_size = 0; }
    [[nodiscard]] usize size() const { return m_size; }
    [[nodiscard]] std::span<const std::byte> bytes() const { return {m_data.data(), m_size}; }
    void reserve(usize bytes) { m_data.resize(bytes); }

    // Appends a record with a fixed payload and `extra` trailing bytes, returns a pointer to the trailing
    // area. Payload and extra are copied with memcpy, so no alignment is needed for writing.
    template <typename Payload>
    std::byte* append(CommandType type, const Payload& payload, usize extraBytes = 0) {
        static_assert(std::is_trivially_copyable_v<Payload>);
        const usize payloadEnd = kPayloadOffset + sizeof(Payload);
        const usize extraOffset = RecordAlignment == 4 ? payloadEnd : alignUp(payloadEnd);
        const usize recordSize = alignUp(extraOffset + extraBytes);
        if (m_size + recordSize > m_data.size()) {
            m_data.resize(std::max(m_data.size() * 2, m_size + recordSize)); // grows during warm-up only
        }
        std::byte* record = m_data.data() + m_size;
        const CommandHeader header{type, static_cast<u16>(recordSize)};
        std::memcpy(record, &header, sizeof(header));
        std::memcpy(record + kPayloadOffset, &payload, sizeof(Payload));
        m_size += recordSize;
        return record + extraOffset;
    }

    // Recording API (what rhi::CommandList would expose)
    void beginRenderPass(const BeginRenderPassCmd& c) { append(CommandType::BeginRenderPass, c); }
    void endRenderPass() { append(CommandType::EndRenderPass, EndRenderPassCmd{}); }
    void bindPipeline(PipelineHandle p) { append(CommandType::BindPipeline, BindPipelineCmd{p}); }
    void bindGroup(u32 index, BindGroupHandle g, std::span<const u32> offsets) {
        std::byte* extra = append(CommandType::BindGroup, BindGroupCmd{index, g, static_cast<u32>(offsets.size())},
                                  offsets.size_bytes());
        std::memcpy(extra, offsets.data(), offsets.size_bytes());
    }
    void bindVertexBuffers(u32 first, std::span<const VertexBufferBinding> b) {
        std::byte* extra = append(CommandType::BindVertexBuffers, BindVertexBuffersCmd{first, static_cast<u32>(b.size())},
                                  b.size_bytes());
        std::memcpy(extra, b.data(), b.size_bytes());
    }
    void setViewport(const SetViewportCmd& c) { append(CommandType::SetViewport, c); }
    void draw(const DrawCmd& c) { append(CommandType::Draw, c); }
    void pushDebugGroup(std::string_view name) {
        std::byte* extra = append(CommandType::PushDebugGroup, PushDebugGroupCmd{static_cast<u32>(name.size())}, name.size());
        std::memcpy(extra, name.data(), name.size());
    }
    void popDebugGroup() { append(CommandType::PopDebugGroup, PopDebugGroupCmd{}); }

private:
    std::vector<std::byte> m_data;
    usize m_size = 0;
};

// Reads a payload: memcpy in the packed variant (records are only 4-byte aligned), in place in the
// aligned one (objects were implicitly created by memcpy into suitably aligned storage).
template <usize RecordAlignment, typename T>
T readPayload(const std::byte* record, usize offset) {
    if constexpr (RecordAlignment == 4) {
        T value;
        std::memcpy(&value, record + offset, sizeof(T));
        return value;
    } else {
        return *std::launder(reinterpret_cast<const T*>(record + offset));
    }
}

template <usize RecordAlignment>
void replay(const CommandStream<RecordAlignment>& stream, MockBackend& backend) {
    using Stream = CommandStream<RecordAlignment>;
    constexpr usize kPayload = Stream::kPayloadOffset;
    const std::span<const std::byte> bytes = stream.bytes();
    usize position = 0;
    while (position < bytes.size()) {
        const std::byte* record = bytes.data() + position;
        CommandHeader header;
        std::memcpy(&header, record, sizeof(header));
        switch (header.type) {
            case CommandType::BeginRenderPass: {
                const auto c = readPayload<RecordAlignment, BeginRenderPassCmd>(record, kPayload);
                backend.count(header.type, c.colorCount);
                break;
            }
            case CommandType::EndRenderPass:
            case CommandType::PopDebugGroup:
                backend.count(header.type, 0);
                break;
            case CommandType::BindPipeline: {
                const auto c = readPayload<RecordAlignment, BindPipelineCmd>(record, kPayload);
                backend.count(header.type, c.pipeline.index());
                break;
            }
            case CommandType::BindGroup: {
                const auto c = readPayload<RecordAlignment, BindGroupCmd>(record, kPayload);
                const usize extra = RecordAlignment == 4 ? kPayload + sizeof(BindGroupCmd)
                                                         : Stream::alignUp(kPayload + sizeof(BindGroupCmd));
                u32 firstOffset = 0;
                if (c.offsetCount > 0) {
                    firstOffset = readPayload<RecordAlignment, u32>(record, extra);
                }
                backend.count(header.type, c.index + c.group.index() + firstOffset);
                break;
            }
            case CommandType::BindVertexBuffers: {
                const auto c = readPayload<RecordAlignment, BindVertexBuffersCmd>(record, kPayload);
                const usize extra = RecordAlignment == 4 ? kPayload + sizeof(BindVertexBuffersCmd)
                                                         : Stream::alignUp(kPayload + sizeof(BindVertexBuffersCmd));
                const auto first = readPayload<RecordAlignment, VertexBufferBinding>(record, extra);
                backend.count(header.type, c.firstSlot + c.count + first.offset);
                break;
            }
            case CommandType::SetViewport: {
                const auto c = readPayload<RecordAlignment, SetViewportCmd>(record, kPayload);
                backend.count(header.type, static_cast<u64>(c.width));
                break;
            }
            case CommandType::Draw: {
                const auto c = readPayload<RecordAlignment, DrawCmd>(record, kPayload);
                backend.count(header.type, c.vertexCount + c.firstVertex);
                break;
            }
            case CommandType::PushDebugGroup: {
                const auto c = readPayload<RecordAlignment, PushDebugGroupCmd>(record, kPayload);
                backend.count(header.type, c.length);
                break;
            }
            default:
                std::abort();
        }
        position += header.size;
    }
}

// ---------------------------------------------------------------------------------------------------
// std::variant alternative

using VariantCommand = std::variant<BeginRenderPassCmd, EndRenderPassCmd, BindPipelineCmd, BindGroupCmd,
                                    BindVertexBuffersCmd, SetViewportCmd, DrawCmd, PushDebugGroupCmd, PopDebugGroupCmd>;

struct VariantList {
    std::vector<VariantCommand> commands;
    std::vector<u32> offsets;                  // dynamic offsets, consumed in order
    std::vector<VertexBufferBinding> bindings; // vertex buffer bindings, consumed in order
    std::string strings;                       // debug group names, consumed in order

    void reset() {
        commands.clear();
        offsets.clear();
        bindings.clear();
        strings.clear();
    }
    void beginRenderPass(const BeginRenderPassCmd& c) { commands.emplace_back(c); }
    void endRenderPass() { commands.emplace_back(EndRenderPassCmd{}); }
    void bindPipeline(PipelineHandle p) { commands.emplace_back(BindPipelineCmd{p}); }
    void bindGroup(u32 index, BindGroupHandle g, std::span<const u32> o) {
        commands.emplace_back(BindGroupCmd{index, g, static_cast<u32>(o.size())});
        offsets.insert(offsets.end(), o.begin(), o.end());
    }
    void bindVertexBuffers(u32 first, std::span<const VertexBufferBinding> b) {
        commands.emplace_back(BindVertexBuffersCmd{first, static_cast<u32>(b.size())});
        bindings.insert(bindings.end(), b.begin(), b.end());
    }
    void setViewport(const SetViewportCmd& c) { commands.emplace_back(c); }
    void draw(const DrawCmd& c) { commands.emplace_back(c); }
    void pushDebugGroup(std::string_view name) {
        commands.emplace_back(PushDebugGroupCmd{static_cast<u32>(name.size())});
        strings.append(name);
    }
    void popDebugGroup() { commands.emplace_back(PopDebugGroupCmd{}); }
};

void replay(const VariantList& list, MockBackend& backend) {
    usize nextOffset = 0;
    usize nextBinding = 0;
    for (const VariantCommand& command : list.commands) {
        std::visit(
            [&](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, BeginRenderPassCmd>) {
                    backend.count(CommandType::BeginRenderPass, c.colorCount);
                } else if constexpr (std::is_same_v<T, EndRenderPassCmd>) {
                    backend.count(CommandType::EndRenderPass, 0);
                } else if constexpr (std::is_same_v<T, BindPipelineCmd>) {
                    backend.count(CommandType::BindPipeline, c.pipeline.index());
                } else if constexpr (std::is_same_v<T, BindGroupCmd>) {
                    const u32 first = c.offsetCount > 0 ? list.offsets[nextOffset] : 0;
                    nextOffset += c.offsetCount;
                    backend.count(CommandType::BindGroup, c.index + c.group.index() + first);
                } else if constexpr (std::is_same_v<T, BindVertexBuffersCmd>) {
                    const u64 offset = list.bindings[nextBinding].offset;
                    nextBinding += c.count;
                    backend.count(CommandType::BindVertexBuffers, c.firstSlot + c.count + offset);
                } else if constexpr (std::is_same_v<T, SetViewportCmd>) {
                    backend.count(CommandType::SetViewport, static_cast<u64>(c.width));
                } else if constexpr (std::is_same_v<T, DrawCmd>) {
                    backend.count(CommandType::Draw, c.vertexCount + c.firstVertex);
                } else if constexpr (std::is_same_v<T, PushDebugGroupCmd>) {
                    backend.count(CommandType::PushDebugGroup, c.length);
                } else {
                    backend.count(CommandType::PopDebugGroup, 0);
                }
            },
            command);
    }
}

// ---------------------------------------------------------------------------------------------------
// Workload: one render pass, 3330 batches of {bindGroup(1 dynamic offset), bindVertexBuffers(2), draw},
// a pipeline change every 50 batches and a debug group every 250: ~10.1k commands.

u32 g_batches = 3330; // batches per frame; the first command-line argument overrides it

template <typename Recorder>
void recordFrame(Recorder& cmd) {
    BeginRenderPassCmd pass{};
    pass.colorCount = 1;
    pass.color[0] = {TextureHandle(0, 1), 1, 0, {0.1F, 0.1F, 0.12F, 1.0F}};
    cmd.beginRenderPass(pass);
    cmd.setViewport({0, 0, 1280, 720, 0, 1});
    for (u32 i = 0; i < g_batches; ++i) {
        if (i % 250 == 0) {
            if (i != 0) {
                cmd.popDebugGroup();
            }
            cmd.pushDebugGroup("Sprite layer");
        }
        if (i % 50 == 0) {
            cmd.bindPipeline(PipelineHandle(i / 50, 1));
        }
        const u32 offset = i * 256;
        cmd.bindGroup(3, BindGroupHandle(7, 1), std::span<const u32>(&offset, 1));
        const std::array<VertexBufferBinding, 2> buffers{
            {{BufferHandle(1, 1), u64{i} * 4096}, {BufferHandle(2, 1), u64{i} * 64}}};
        cmd.bindVertexBuffers(0, buffers);
        cmd.draw({6, 1, i * 6, 0});
    }
    cmd.popDebugGroup();
    cmd.endRenderPass();
}

u64 totalCalls(const MockBackend& b) {
    u64 sum = 0;
    for (u64 c : b.calls) {
        sum += c;
    }
    return sum;
}

using Clock = std::chrono::steady_clock;

struct Timing {
    double recordUs;
    double replayUs;
};

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

constexpr int kWarmup = 50;
constexpr int kFrames = 1000;

template <typename List>
Timing measure(List& list, u64& commands, u64& checksum, u64& allocations, usize& bytes) {
    std::vector<double> record;
    std::vector<double> replayTimes;
    record.reserve(kFrames);
    replayTimes.reserve(kFrames);
    u64 allocationsBefore = 0;
    for (int frame = 0; frame < kWarmup + kFrames; ++frame) {
        if (frame == kWarmup) {
            allocationsBefore = g_allocations.load();
        }
        MockBackend backend;
        const auto t0 = Clock::now();
        list.reset();
        recordFrame(list);
        const auto t1 = Clock::now();
        replay(list, backend);
        const auto t2 = Clock::now();
        if (frame >= kWarmup) {
            record.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
            replayTimes.push_back(std::chrono::duration<double, std::micro>(t2 - t1).count());
        }
        commands = totalCalls(backend);
        checksum = backend.checksum;
    }
    // record/replayTimes were reserved up front, so only the lists themselves could allocate.
    allocations = g_allocations.load() - allocationsBefore;
    if constexpr (requires { list.size(); }) {
        bytes = list.size();
    } else {
        bytes = list.commands.size() * sizeof(VariantCommand) + list.offsets.size() * sizeof(u32) +
                list.bindings.size() * sizeof(VertexBufferBinding) + list.strings.size();
    }
    return {median(record), median(replayTimes)};
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        g_batches = static_cast<u32>(std::strtoul(argv[1], nullptr, 10));
    }
    std::printf("sizeof(VariantCommand) = %zu, sizeof(BeginRenderPassCmd) = %zu\n\n", sizeof(VariantCommand),
                sizeof(BeginRenderPassCmd));
    std::printf("%-8s %10s %12s %12s %14s %10s %18s\n", "variant", "commands", "record us", "replay us",
                "ns/cmd (r+p)", "bytes", "allocs after warm");

    u64 referenceChecksum = 0;
    const auto report = [&](const char* name, Timing t, u64 commands, u64 checksum, u64 allocations, usize bytes) {
        if (referenceChecksum == 0) {
            referenceChecksum = checksum;
        } else if (checksum != referenceChecksum) {
            std::printf("checksum mismatch in %s!\n", name);
        }
        std::printf("%-8s %10llu %12.1f %12.1f %14.2f %10zu %18llu\n", name, static_cast<unsigned long long>(commands),
                    t.recordUs, t.replayUs, (t.recordUs + t.replayUs) * 1000.0 / static_cast<double>(commands), bytes,
                    static_cast<unsigned long long>(allocations));
    };

    {
        CommandStream<4> packed;
        u64 commands = 0, checksum = 0, allocations = 0;
        usize bytes = 0;
        const Timing t = measure(packed, commands, checksum, allocations, bytes);
        report("packed", t, commands, checksum, allocations, bytes);
    }
    {
        CommandStream<8> aligned;
        u64 commands = 0, checksum = 0, allocations = 0;
        usize bytes = 0;
        const Timing t = measure(aligned, commands, checksum, allocations, bytes);
        report("aligned", t, commands, checksum, allocations, bytes);
    }
    {
        VariantList variant;
        u64 commands = 0, checksum = 0, allocations = 0;
        usize bytes = 0;
        const Timing t = measure(variant, commands, checksum, allocations, bytes);
        report("variant", t, commands, checksum, allocations, bytes);
    }
    {
        // Direct calls: "record" is the virtual calls themselves; nothing to replay.
        MockVirtualBackend backend;
        std::vector<double> times;
        times.reserve(kFrames);
        for (int frame = 0; frame < kWarmup + kFrames; ++frame) {
            backend.counter = {};
            const auto t0 = Clock::now();
            Backend& b = backend;
            recordFrame(b);
            const auto t1 = Clock::now();
            if (frame >= kWarmup) {
                times.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
            }
        }
        report("direct", {median(times), 0.0}, totalCalls(backend.counter), backend.counter.checksum, 0, 0);
    }
    return 0;
}
