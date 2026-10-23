// teserra_shaderc: compiles a GLSL 450 shader to SPIR-V at build time (ADR-0017). See cli.hpp.

#include "cli.hpp"

#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    try {
        const std::vector<std::string_view> args(argv + 1, argv + argc);
        return teserra::shaderc::run(args, std::cout, std::cerr);
    } catch (...) { // run() reports its own errors; this only covers running out of memory before it
        return teserra::shaderc::kExitUsageError;
    }
}
