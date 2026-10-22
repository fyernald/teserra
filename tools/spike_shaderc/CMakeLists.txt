# SPIKE #36 (never merged): glslang + SPIRV-Cross on the shader pipeline of ADR-0017.
find_package(glslang CONFIG REQUIRED)
find_package(spirv_cross_core CONFIG REQUIRED)
find_package(spirv_cross_glsl CONFIG REQUIRED)

add_executable(teserra_spike_shaderc main.cpp)
target_link_libraries(teserra_spike_shaderc PRIVATE
    glslang::glslang glslang::glslang-default-resource-limits glslang::SPIRV
    spirv-cross-glsl spirv-cross-core
    teserra_platform teserra_glad)
