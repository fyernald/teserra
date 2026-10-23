# Fixture project for teserra_add_shaders() in cmake/TeserraShaders.cmake. tests/cmake/shaders/
# TestShaderBuild.cmake copies it to a scratch directory, builds it and edits its shaders.
# TESERRA_SOURCE_DIR is the Teserra checkout, TESERRA_SHADERC the built teserra_shaderc executable.

cmake_minimum_required(VERSION 3.28)
project(TeserraShadersFixture LANGUAGES CXX)

include("${TESERRA_SOURCE_DIR}/cmake/TeserraShaders.cmake")

add_executable(teserra_shaderc IMPORTED)
set_target_properties(teserra_shaderc PROPERTIES IMPORTED_LOCATION "${TESERRA_SHADERC}")

# Embedded: main.cpp checks the data.
add_executable(embedded main.cpp)
target_compile_features(embedded PRIVATE cxx_std_20)
teserra_add_shaders(embedded EMBED
    NAMESPACE fixture::shaders
    HEADER fixture/shaders.hpp
    INCLUDE_DIRECTORIES shaders/include
    DEFINES FIXTURE_TINT=0.5
    SOURCES shaders/a.vert shaders/b.frag
)

# Shader files for a game to load.
add_custom_target(assets ALL)
teserra_add_shaders(assets OUTPUT_DIRECTORY assets INCLUDE_DIRECTORIES shaders/include SOURCES shaders/a.vert)

if(TESERRA_FIXTURE_CASE STREQUAL "duplicate_names")
    add_executable(duplicates main.cpp)
    teserra_add_shaders(duplicates EMBED SOURCES shaders/b.frag other/b.frag)
endif()
