#pragma once

// A frame arena: transient memory that is handed out by bumping a pointer and given back all at once
// (docs/architecture/memory-and-performance.md#allocation-strategy).
//
//   LinearArena arena;                                    // one per frame slot if data outlives a frame
//   ...
//   arena.reset();                                        // at the start of the frame
//   std::pmr::vector<Entity> hits(&arena);                // any std::pmr container
//   std::pmr::vector<std::pmr::vector<u32>> cells(1024, &arena);
//
// Containers built on the arena must not outlive the next reset(). Deallocation is free and does nothing;
// the memory comes back at reset(). Data that the GPU or a later frame still reads needs one arena per frame
// slot (kMaxFramesInFlight), each reset when its slot comes round again.
//
// Unlike std::pmr::monotonic_buffer_resource, whose release() returns its blocks to the upstream resource,
// reset() keeps the memory: when a frame needed more than one block, they are merged into one block of the
// total size, so after a warm-up frame of the largest size every frame runs without upstream allocations.
//
// When TESERRA_ENABLE_ASSERTS is on, reset() fills the memory it takes back with kPoison, and deallocating a
// pointer that was not handed out since the last reset asserts: a container that outlived reset() is caught
// when it grows or is destroyed. With AddressSanitizer the memory is also poisoned for ASan, so any read or
// write through a stale pointer stops the program.

#include <teserra/core/types.hpp>

#include <cstddef>
#include <memory_resource>
#include <vector>

namespace teserra {

/// A linear (bump) allocator as a std::pmr::memory_resource. Not thread-safe; not copyable or movable,
/// because containers refer to it by address.
class LinearArena final : public std::pmr::memory_resource {
public:
    /// The byte reset() writes over released memory when assertions are on.
    static constexpr std::byte kPoison{0xDD};
    /// The size of the first block, and of further blocks unless an allocation needs more.
    static constexpr usize kDefaultBlockSize = usize{64} * 1024;

    /// An empty arena; the first allocation takes a block of `blockSize` bytes (at least) from `upstream`,
    /// which must outlive the arena.
    explicit LinearArena(usize blockSize = kDefaultBlockSize,
                         std::pmr::memory_resource* upstream = std::pmr::new_delete_resource());
    ~LinearArena() override;

    LinearArena(const LinearArena&) = delete;
    LinearArena& operator=(const LinearArena&) = delete;
    LinearArena(LinearArena&&) = delete;
    LinearArena& operator=(LinearArena&&) = delete;

    /// Takes back everything allocated so far and keeps the memory. If more than one block was in use,
    /// they are replaced by a single block of their total size.
    void reset();

    /// Takes back everything and returns all memory to the upstream resource.
    void release() noexcept;

    /// Bytes handed out since the last reset, alignment padding included.
    [[nodiscard]] usize used() const noexcept { return m_used; }
    /// Bytes held from the upstream resource.
    [[nodiscard]] usize capacity() const noexcept;
    /// Blocks held from the upstream resource: at most one right after reset().
    [[nodiscard]] usize blockCount() const noexcept { return m_blocks.size(); }

private:
    struct Block {
        std::byte* data;
        usize size;
    };

    void* do_allocate(usize bytes, usize alignment) override;
    void do_deallocate(void* pointer, usize bytes, usize alignment) override;
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

    // True if [pointer, pointer + bytes) lies in memory handed out since the last reset.
    [[nodiscard]] bool isLive(const void* pointer, usize bytes) const noexcept;
    void addBlock(usize size);
    void freeBlocks() noexcept;

    std::pmr::memory_resource* m_upstream;
    usize m_blockSize;
    std::vector<Block> m_blocks;
    usize m_offset = 0; // bytes used in the last block, the one being filled
    usize m_used = 0;   // bytes handed out since the last reset
};

} // namespace teserra
