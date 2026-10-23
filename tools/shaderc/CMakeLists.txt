# teserra_shaderc: the build-time shader compiler (ADR-0017). It compiles GLSL 450 (Vulkan dialect) to
# SPIR-V with glslang. A host tool, not an engine module: games never link it, and it uses the standard
# library only.
#
#   teserra_shaderc_compiler   static library with the compiler and the command line (src/compiler.hpp,
#                              src/cli.hpp); the only code that includes glslang
#   teserra_shaderc            the executable

find_package(glslang CONFIG REQUIRED)

# The language level and warnings of engine modules.
function(_teserra_configure_shaderc_target target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    teserra_set_warnings(${target})
endfunction()

add_library(teserra_shaderc_compiler STATIC
    src/cli.cpp
    src/cli.hpp
    src/compiler.cpp
    src/compiler.hpp
)
_teserra_configure_shaderc_target(teserra_shaderc_compiler)
target_compile_features(teserra_shaderc_compiler PUBLIC cxx_std_20)
target_include_directories(teserra_shaderc_compiler PUBLIC src)
target_link_libraries(teserra_shaderc_compiler PRIVATE
    glslang::glslang
    glslang::glslang-default-resource-limits
    glslang::SPIRV
)

add_executable(teserra_shaderc src/main.cpp)
_teserra_configure_shaderc_target(teserra_shaderc)
target_link_libraries(teserra_shaderc PRIVATE teserra_shaderc_compiler)

if(TESERRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
