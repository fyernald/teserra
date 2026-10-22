add_executable(teserra_window_input main.cpp)
target_link_libraries(teserra_window_input PRIVATE Teserra::Engine)
teserra_set_warnings(teserra_window_input)

# Smoke run (docs/guides/testing-strategy.md#example-smoke-runs): 120 frames in a hidden window must exit
# with 0 and log nothing at Error or above. Machines without an OpenGL 4.5 driver (the CI runners) cannot
# create the window's context; the run is reported as skipped there, like the platform's OpenGL test.
if(TESERRA_BUILD_TESTS)
    add_test(NAME examples.window_input COMMAND teserra_window_input --frames 120 --hidden-window)
    set_tests_properties(examples.window_input PROPERTIES
        # "[error]" / "[critical]" level tags; [[] matches a literal '[' without escaping.
        FAIL_REGULAR_EXPRESSION "[[](error|critical)]"
        SKIP_REGULAR_EXPRESSION "OpenGL 4[.]5 core context"
        LABELS "examples"
    )
endif()
