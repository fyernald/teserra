// Prints the version of the linked engine library. The smallest program that proves the build works.

#include <teserra/core/version.hpp>

#include <iostream>

int main() {
    // Program output, not diagnostics, so it goes to stdout rather than through the logging facade.
    std::cout << "Teserra " << teserra::versionString() << '\n';
    return 0;
}
