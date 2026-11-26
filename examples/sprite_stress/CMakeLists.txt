add_executable(teserra_sprite_stress main.cpp)
target_link_libraries(teserra_sprite_stress PRIVATE Teserra::Engine Teserra::Renderer)
teserra_set_warnings(teserra_sprite_stress)

# Smoke run (docs/guides/testing-strategy.md#example-smoke-runs): 600 frames of the default 1000 sprites in a
# hidden window must exit with 0 and log nothing at Error or above, which includes OpenGL debug errors and RHI
# validation. 10 000 sprites take a minute in a Debug build; run `--sprites 10000` by hand for that. Machines without an OpenGL 4.5 driver (the CI runners) cannot create the window's context; the
# run is reported as skipped there. It runs in its own directory, where it writes teserra.log.
if(TESERRA_BUILD_TESTS)
    add_test(NAME examples.sprite_stress
             COMMAND teserra_sprite_stress --frames 600 --hidden-window
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(examples.sprite_stress PROPERTIES
        # "[error]" / "[critical]" level tags; [[] matches a literal '[' without escaping.
        FAIL_REGULAR_EXPRESSION "[[](error|critical)]"
        SKIP_REGULAR_EXPRESSION "${TESERRA_SMOKE_SKIP_REGEX}"
        LABELS "examples"
    )
endif()
