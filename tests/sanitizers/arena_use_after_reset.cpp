// Reads arena memory after LinearArena::reset(), for the sanitizer job (#136): built only with
// TESERRA_ENABLE_SANITIZERS, the test passes when AddressSanitizer stops the program with a use-after-poison
// report, because reset() poisons the memory it takes back.

#include <teserra/core/linear_arena.hpp>

#include <cstdio>

int main() {
    teserra::LinearArena arena;
    auto* const values = static_cast<int*>(arena.allocate(8 * sizeof(int), alignof(int)));
    values[3] = 42; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    arena.reset();
    // volatile: the compiler must not drop the read.
    const volatile int read = values[3]; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    std::printf("read %d after reset: the sanitizer missed it\n", read);
    return 0;
}
