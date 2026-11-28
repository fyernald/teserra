#include <teserra/core/linear_arena.hpp>

#include <teserra/core/assert.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>

// AddressSanitizer: MSVC and GCC define __SANITIZE_ADDRESS__, Clang reports it through __has_feature.
#if defined(__SANITIZE_ADDRESS__)
#define TESERRA_ARENA_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TESERRA_ARENA_ASAN 1
#endif
#endif

#if defined(TESERRA_ARENA_ASAN)
#include <sanitizer/asan_interface.h>
#endif

namespace teserra {

namespace {

// Blocks come from the upstream resource with the alignment every fundamental type needs; larger alignments
// are reached by padding inside the block.
constexpr usize kBlockAlignment = alignof(std::max_align_t);

// Memory the arena holds but has not handed out: poisoned for ASan, filled with kPoison when asserting.
void poison(std::byte* data, usize size) noexcept {
    if (size == 0) {
        return;
    }
#if TESERRA_ENABLE_ASSERTS
    std::memset(data, std::to_integer<int>(LinearArena::kPoison), size);
#endif
#if defined(TESERRA_ARENA_ASAN)
    ASAN_POISON_MEMORY_REGION(data, size);
#endif
    static_cast<void>(data);
}

void unpoison([[maybe_unused]] std::byte* data, [[maybe_unused]] usize size) noexcept {
#if defined(TESERRA_ARENA_ASAN)
    ASAN_UNPOISON_MEMORY_REGION(data, size);
#endif
}

[[nodiscard]] std::uintptr_t address(const void* pointer) noexcept {
    return reinterpret_cast<std::uintptr_t>(pointer); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
}

} // namespace

LinearArena::LinearArena(usize blockSize, std::pmr::memory_resource* upstream)
    : m_upstream(upstream), m_blockSize(std::max<usize>(blockSize, 1)) {
    TESERRA_ASSERT(upstream != nullptr, "LinearArena needs an upstream resource");
}

LinearArena::~LinearArena() {
    freeBlocks();
}

void LinearArena::reset() {
    if (m_blocks.size() > 1) {
        // A frame outgrew the block: one block of the total size serves frames like it from now on.
        const usize total = capacity();
        freeBlocks();
        addBlock(total);
    } else if (!m_blocks.empty()) {
        poison(m_blocks.front().data, m_offset);
    }
    m_offset = 0;
    m_used = 0;
}

void LinearArena::release() noexcept {
    freeBlocks();
    m_offset = 0;
    m_used = 0;
}

usize LinearArena::capacity() const noexcept {
    usize total = 0;
    for (const Block& block : m_blocks) {
        total += block.size;
    }
    return total;
}

void* LinearArena::do_allocate(usize bytes, usize alignment) {
    TESERRA_ASSERT(std::has_single_bit(alignment), "LinearArena: alignment {} is not a power of two",
                   alignment);
    bytes = std::max<usize>(bytes, 1); // distinct pointers for empty allocations

    if (!m_blocks.empty()) {
        const Block& block = m_blocks.back();
        const std::uintptr_t base = address(block.data);
        const usize start = ((base + m_offset + alignment - 1) & ~(alignment - 1)) - base;
        if (start <= block.size && bytes <= block.size - start) {
            m_used += start + bytes - m_offset;
            m_offset = start + bytes;
            unpoison(block.data + start, bytes);
            return block.data + start;
        }
    }

    // A new block, large enough for this allocation and its alignment padding.
    const usize padding = alignment > kBlockAlignment ? alignment - 1 : 0;
    addBlock(std::max(m_blockSize, bytes + padding));
    const Block& block = m_blocks.back();
    const std::uintptr_t base = address(block.data);
    const usize start = ((base + alignment - 1) & ~(alignment - 1)) - base;
    m_used += start + bytes;
    m_offset = start + bytes;
    unpoison(block.data + start, bytes);
    return block.data + start;
}

void LinearArena::do_deallocate(void* pointer, usize bytes, usize alignment) {
    // The memory comes back at reset(). A pointer from before the last reset means a container outlived it.
    TESERRA_ASSERT(pointer == nullptr || isLive(pointer, bytes),
                   "LinearArena: deallocating {} bytes at {} that were not allocated since the last reset "
                   "(did a container outlive reset()?)",
                   bytes, pointer);
    static_cast<void>(alignment);
}

bool LinearArena::do_is_equal(const std::pmr::memory_resource& other) const noexcept {
    return this == &other;
}

bool LinearArena::isLive(const void* pointer, usize bytes) const noexcept {
    const std::uintptr_t begin = address(pointer);
    for (usize index = 0; index < m_blocks.size(); ++index) {
        const std::uintptr_t blockBegin = address(m_blocks[index].data);
        // Earlier blocks are full; in the last one only the first m_offset bytes are handed out.
        const usize live = index + 1 == m_blocks.size() ? m_offset : m_blocks[index].size;
        if (begin >= blockBegin && begin - blockBegin < live) {
            return std::max<usize>(bytes, 1) <= live - (begin - blockBegin);
        }
    }
    return false;
}

void LinearArena::addBlock(usize size) {
    m_blocks.reserve(m_blocks.size() + 1); // before allocating, so a throw cannot leak the block
    auto* const data = static_cast<std::byte*>(m_upstream->allocate(size, kBlockAlignment));
    m_blocks.push_back({.data = data, .size = size});
    m_offset = 0;
    poison(data, size);
}

void LinearArena::freeBlocks() noexcept {
    for (const Block& block : m_blocks) {
        unpoison(block.data, block.size);
        m_upstream->deallocate(block.data, block.size, kBlockAlignment);
    }
    m_blocks.clear();
}

} // namespace teserra
