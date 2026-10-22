// Replaces the global allocation functions in every test executable, counting allocations per thread for
// <teserra/test/allocation_counter.hpp>. The nothrow forms are not replaced: the standard ones call the
// throwing forms below.

#include <teserra/test/allocation_counter.hpp>

#include <cstddef>
#include <cstdlib>
#include <new>

namespace {

thread_local teserra::u64 threadAllocations = 0;

void* allocate(std::size_t size) {
    ++threadAllocations;
    void* memory = std::malloc(size == 0 ? 1 : size); // operator new(0) returns a unique pointer
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    return memory;
}

void* allocateAligned(std::size_t size, std::align_val_t alignment) {
    ++threadAllocations;
    const auto bytes = static_cast<std::size_t>(alignment);
#if defined(_MSC_VER)
    void* memory = _aligned_malloc(size == 0 ? 1 : size, bytes);
#else
    // aligned_alloc requires a size that is a multiple of the alignment.
    void* memory = std::aligned_alloc(bytes, size == 0 ? bytes : (size + bytes - 1) / bytes * bytes);
#endif
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    return memory;
}

void freeAligned(void* memory) noexcept {
#if defined(_MSC_VER)
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}

} // namespace

namespace teserra::test {

u64 threadAllocationCount() noexcept {
    return threadAllocations;
}

} // namespace teserra::test

void* operator new(std::size_t size) {
    return allocate(size);
}

void* operator new[](std::size_t size) {
    return allocate(size);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocateAligned(size, alignment);
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocateAligned(size, alignment);
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t /*size*/) noexcept {
    std::free(memory);
}

void operator delete[](void* memory, std::size_t /*size*/) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::align_val_t /*alignment*/) noexcept {
    freeAligned(memory);
}

void operator delete[](void* memory, std::align_val_t /*alignment*/) noexcept {
    freeAligned(memory);
}

void operator delete(void* memory, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept {
    freeAligned(memory);
}

void operator delete[](void* memory, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept {
    freeAligned(memory);
}
