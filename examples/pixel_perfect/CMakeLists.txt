add_executable(teserra_pixel_perfect main.cpp)
target_link_libraries(teserra_pixel_perfect PRIVATE Teserra::Engine)
teserra_set_warnings(teserra_pixel_perfect)

# Smoke run (docs/guides/testing-strategy.md#example-smoke-runs): 120 frames in a hidden window must exit with 0
# and log nothing at Error or above, which includes OpenGL debug errors and RHI validation. Machines without
# an OpenGL 4.5 driver (the CI runners) cannot create the window's context; the run is reported as skipped
# there.
if(TESERRA_BUILD_TESTS)
    add_test(NAME examples.pixel_perfect
             COMMAND teserra_pixel_perfect --frames 120 --hidden-window
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(examples.pixel_perfect PROPERTIES
        # "[error]" / "[critical]" level tags; [[] matches a literal '[' without escaping.
        FAIL_REGULAR_EXPRESSION "[[](error|critical)]"
        SKIP_REGULAR_EXPRESSION "OpenGL 4[.]5 core context"
        LABELS "examples"
    )
endif()
