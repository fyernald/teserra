// The one translation unit that compiles miniaudio's implementation (ADR-0027).
#include <teserra/core/assert.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <new>

// miniaudio's assertions are the engine's.
#define MA_ASSERT(condition) TESERRA_ASSERT(condition)
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio_config.hpp"

namespace teserra::audio::detail {

namespace {

// Every allocation starts with a header that holds its size, for realloc; the header keeps the
// alignment of operator new.
constexpr std::size_t kHeaderSize = alignof(std::max_align_t);

void* allocate(std::size_t size, void* /*userData*/) noexcept {
    void* block = ::operator new(size + kHeaderSize, std::nothrow);
    if (block == nullptr) {
        return nullptr;
    }
    std::memcpy(block, &size, sizeof(size));
    return static_cast<std::byte*>(block) + kHeaderSize;
}

void release(void* memory, void* /*userData*/) noexcept {
    if (memory != nullptr) {
        ::operator delete(static_cast<std::byte*>(memory) - kHeaderSize);
    }
}

void* reallocate(void* memory, std::size_t size, void* userData) noexcept {
    if (memory == nullptr) {
        return allocate(size, userData);
    }
    std::size_t oldSize = 0;
    std::memcpy(&oldSize, static_cast<std::byte*>(memory) - kHeaderSize, sizeof(oldSize));
    void* resized = allocate(size, userData);
    if (resized != nullptr) {
        // Parenthesized: miniaudio includes <windows.h>, whose min macro would expand here.
        std::memcpy(resized, memory, (std::min)(oldSize, size));
        release(memory, userData);
    }
    return resized;
}

} // namespace

ma_allocation_callbacks allocationCallbacks() noexcept {
    return {.pUserData = nullptr, .onMalloc = allocate, .onRealloc = reallocate, .onFree = release};
}

} // namespace teserra::audio::detail
