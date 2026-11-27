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

#if !defined(MA_HAS_VORBIS)
#error "miniaudio was compiled without stb_vorbis's declarations before it: it would not decode OGG Vorbis"
#endif

// stb_vorbis's implementation, after miniaudio's, as miniaudio documents.
#undef STB_VORBIS_HEADER_ONLY
// MSVC reports possibly uninitialized locals in stb_vorbis when it generates code, which /external:W0 does
// not cover; stb_vorbis initializes them on every path that reads them.
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(push)
#pragma warning(disable : 4701 4703)
#endif
#include <stb_vorbis.c> // NOLINT(bugprone-suspicious-include)
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(pop)
#endif

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
