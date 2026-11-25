// Reads memory after freeing it, for the sanitizer job (#113): built only with TESERRA_ENABLE_SANITIZERS, the
// test passes when AddressSanitizer stops the program with a heap-use-after-free report.

#include <cstdio>
#include <memory>

int main() {
    auto values = std::make_unique<int[]>(8); // NOLINT(cppcoreguidelines-avoid-c-arrays)
    int* stale = values.get();
    values.reset();
    // volatile: the compiler must not drop the read.
    const volatile int read = stale[3]; // NOLINT(clang-analyzer-cplusplus.NewDelete)
    std::printf("read %d after free: the sanitizer missed it\n", read);
    return 0;
}
