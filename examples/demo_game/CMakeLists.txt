add_executable(teserra_demo_game main.cpp art.cpp art.hpp flow.cpp flow.hpp font.cpp font.hpp gameplay.cpp gameplay.hpp)
target_link_libraries(teserra_demo_game PRIVATE Teserra::Engine)
teserra_set_warnings(teserra_demo_game)
# The game is dogfooding: it may use only the public API.
teserra_check_public_includes(teserra_demo_game)

# Smoke run (docs/guides/testing-strategy.md#example-smoke-runs): 600 frames in a hidden window must exit with 0
# and log nothing at Error or above, which includes OpenGL debug errors and RHI validation. Machines without
# an OpenGL 4.5 driver (the CI runners) cannot create the window's context; the run is reported as skipped
# there.
if(TESERRA_BUILD_TESTS)
    add_test(NAME examples.demo_game
             COMMAND teserra_demo_game --frames 600 --hidden-window
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(examples.demo_game PROPERTIES
        # "[error]" / "[critical]" level tags; [[] matches a literal '[' without escaping.
        FAIL_REGULAR_EXPRESSION "[[](error|critical)]"
        SKIP_REGULAR_EXPRESSION "OpenGL 4[.]5 core context"
        LABELS "examples"
    )
endif()
