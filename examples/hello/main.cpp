// Prints the version of the linked engine library. The smallest program that proves the build works.

#include <teserra/core/version.hpp>

#include <cstdio>
#include <string_view>

int main() {
    // Program output, not diagnostics, so it goes to stdout rather than through the logging facade.
    // printf instead of iostreams: it cannot throw, so main() cannot exit through an exception.
    const std::string_view version = teserra::versionString();
    std::printf("Teserra %.*s\n", static_cast<int>(version.size()), version.data());
    return 0;
}
