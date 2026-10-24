teserra_add_module(assets LAYER 2
    DEPENDS core
    PUBLIC_HEADERS
        include/teserra/assets/image.hpp
    SOURCES
        src/image.cpp
)

# stb_image decodes images (ADR-0010). It is PRIVATE: src/image.cpp is the one translation unit that
# compiles it. stb is a header with no CMake package: the vcpkg port's FindStb.cmake is only reachable
# through the vcpkg toolchain, so the header is found directly, which also works from a plain prefix path
# and with distributions that install it to include/stb/. An imported target carries the directory; the
# include directories of imported targets are SYSTEM, so stb's own warnings do not count.
find_path(TESERRA_STB_INCLUDE_DIR NAMES stb_image.h PATH_SUFFIXES stb REQUIRED)
mark_as_advanced(TESERRA_STB_INCLUDE_DIR)
add_library(teserra_stb_image INTERFACE IMPORTED)
set_target_properties(teserra_stb_image PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${TESERRA_STB_INCLUDE_DIR}")
teserra_link_third_party(assets PRIVATE teserra_stb_image)

if(TESERRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
