add_executable(teserra_textured_quad main.cpp)
target_link_libraries(teserra_textured_quad PRIVATE Teserra::Engine Teserra::Assets)
teserra_set_warnings(teserra_textured_quad)

# The shaders are compiled at build time and embedded: <textured_quad/shaders.hpp> declares
# teserra::examples::shaders::kQuadVert and kQuadFrag.
teserra_add_shaders(teserra_textured_quad EMBED
    NAMESPACE teserra::examples::shaders
    HEADER textured_quad/shaders.hpp
    SOURCES
        shaders/quad.vert
        shaders/quad.frag
)

# The example reads its image from assets/ next to the executable. The copy runs on every build and only
# writes when the file changed, so an edited image reaches the build without relinking the example.
teserra_copy_assets(teserra_textured_quad)

# Smoke run (docs/guides/testing-strategy.md#example-smoke-runs): 120 frames in a hidden window must exit with
# 0 and log nothing at Error or above, which includes OpenGL debug errors and RHI validation. Machines without
# an OpenGL 4.5 driver (the CI runners) cannot create the window's context; the run is reported as skipped
# there.
if(TESERRA_BUILD_TESTS)
    add_test(NAME examples.textured_quad COMMAND teserra_textured_quad --frames 120 --hidden-window
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(examples.textured_quad PROPERTIES
        # "[error]" / "[critical]" level tags; [[] matches a literal '[' without escaping.
        FAIL_REGULAR_EXPRESSION "[[](error|critical)]"
        SKIP_REGULAR_EXPRESSION "OpenGL 4[.]5 core context"
        LABELS "examples"
    )
endif()
