// Checks the shaders that teserra_add_shaders() embedded: .tshader blobs, 4-byte aligned.

#include <fixture/shaders.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

int main() {
    for (const std::span<const std::byte> blob : {fixture::shaders::kAVert, fixture::shaders::kBFrag}) {
        if (blob.size() < 16 || blob[0] != std::byte{'T'} || blob[1] != std::byte{'S'} ||
            blob[2] != std::byte{'H'} || blob[3] != std::byte{'B'}) {
            std::puts("not a .tshader blob");
            return 1;
        }
        if (reinterpret_cast<std::uintptr_t>(blob.data()) % 4 != 0) {
            std::puts("not 4-byte aligned");
            return 2;
        }
    }
    std::puts("embedded shaders ok");
    return 0;
}
