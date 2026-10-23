add_executable(teserra_rhi_triangle main.cpp)
target_link_libraries(teserra_rhi_triangle PRIVATE Teserra::Engine)
teserra_set_warnings(teserra_rhi_triangle)

# The shaders are compiled at build time and embedded: <rhi_triangle/shaders.hpp> declares
# teserra::examples::shaders::kTriangleVert and kTriangleFrag.
teserra_add_shaders(teserra_rhi_triangle EMBED
    NAMESPACE teserra::examples::shaders
    HEADER rhi_triangle/shaders.hpp
    SOURCES
        shaders/triangle.vert
        shaders/triangle.frag
)

# Smoke run (docs/guides/testing-strategy.md#example-smoke-runs): 120 frames in a hidden window must exit with
# 0 and log nothing at Error or above, which includes OpenGL debug errors and RHI validation. Machines without
# an OpenGL 4.5 driver (the CI runners) cannot create the window's context; the run is reported as skipped
# there. It runs in its own directory, where it writes teserra.log.
if(TESERRA_BUILD_TESTS)
    add_test(NAME examples.rhi_triangle COMMAND teserra_rhi_triangle --frames 120 --hidden-window
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(examples.rhi_triangle PROPERTIES
        # "[error]" / "[critical]" level tags; [[] matches a literal '[' without escaping.
        FAIL_REGULAR_EXPRESSION "[[](error|critical)]"
        SKIP_REGULAR_EXPRESSION "OpenGL 4[.]5 core context"
        LABELS "examples"
    )
endif()
