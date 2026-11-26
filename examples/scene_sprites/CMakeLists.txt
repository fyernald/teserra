add_executable(teserra_scene_sprites main.cpp)
target_link_libraries(teserra_scene_sprites PRIVATE Teserra::Engine)
teserra_set_warnings(teserra_scene_sprites)

# The example reads its sprite sheet as assets://sprites.png, which the App mounts at assets/ next to the
# executable. The copy runs on every build and only writes when the file changed.
teserra_copy_assets(teserra_scene_sprites)

# Smoke run (docs/guides/testing-strategy.md#example-smoke-runs): 120 frames in a hidden window must exit with 0
# and log nothing at Error or above, which includes OpenGL debug errors and RHI validation. Machines without
# an OpenGL 4.5 driver (the CI runners) cannot create the window's context; the run is reported as skipped
# there.
if(TESERRA_BUILD_TESTS)
    add_test(NAME examples.scene_sprites
             COMMAND teserra_scene_sprites --frames 120 --hidden-window
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(examples.scene_sprites PROPERTIES
        # "[error]" / "[critical]" level tags; [[] matches a literal '[' without escaping.
        FAIL_REGULAR_EXPRESSION "[[](error|critical)]"
        SKIP_REGULAR_EXPRESSION "${TESERRA_SMOKE_SKIP_REGEX}"
        LABELS "examples"
    )
endif()
