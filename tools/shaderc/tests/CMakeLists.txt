set(fixtures "${CMAKE_CURRENT_SOURCE_DIR}/shaders")

teserra_add_test(shaderc
    SOURCES
        cli_test.cpp
        compiler_test.cpp
        spirv_validation.cpp
        spirv_validation.hpp
        translator_test.cpp
    LIBRARIES teserra_shaderc_compiler
)
target_compile_definitions(teserra_shaderc_tests PRIVATE
    "TESERRA_SHADERC_FIXTURES=\"${fixtures}\""
    "TESERRA_SHADERC_TEST_OUTPUT=\"${CMAKE_CURRENT_BINARY_DIR}/output\""
)

# SPIR-V validation with SPIRV-Tools, the library behind spirv-val (ADR-0017): opt-in because it takes long
# to build. Without it, the validation checks are skipped.
if(TESERRA_SHADER_VALIDATION)
    find_package(SPIRV-Tools CONFIG REQUIRED)
    target_link_libraries(teserra_shaderc_tests PRIVATE SPIRV-Tools-static)
    target_compile_definitions(teserra_shaderc_tests PRIVATE TESERRA_HAS_SPIRV_TOOLS=1)
else()
    target_compile_definitions(teserra_shaderc_tests PRIVATE TESERRA_HAS_SPIRV_TOOLS=0)
endif()

# The executable itself, the way a build runs it: exit codes and a clickable error.
add_test(NAME shaderc.cli.compiles
    COMMAND teserra_shaderc "${fixtures}/triangle.vert" -o "${CMAKE_CURRENT_BINARY_DIR}/cli/triangle.vert.tshader"
)
add_test(NAME shaderc.cli.reports_errors
    COMMAND "${CMAKE_COMMAND}"
        "-DTESERRA_PROGRAM=$<TARGET_FILE:teserra_shaderc>"
        "-DTESERRA_ARGS=${fixtures}/errors/syntax_error.frag$<SEMICOLON>-o$<SEMICOLON>${CMAKE_CURRENT_BINARY_DIR}/cli/syntax_error.frag.tshader$<SEMICOLON>--diagnostics$<SEMICOLON>msvc"
        "-DTESERRA_EXPECT=syntax_error\\.frag\\(7\\): error: 'undeclaredColor' : undeclared identifier"
        -P "${PROJECT_SOURCE_DIR}/cmake/scripts/ExpectAbort.cmake"
)
set_tests_properties(shaderc.cli.compiles shaderc.cli.reports_errors PROPERTIES LABELS "shaderc")
