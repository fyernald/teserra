# templates/minimal_game/src/main.cpp built against this tree (#120): a change that breaks the template breaks
# the build, with the engine's warnings and the public-API check that games get. The template's own
# CMakeLists.txt is built by CI with Teserra from FetchContent.
set(template "${PROJECT_SOURCE_DIR}/templates/minimal_game")

add_executable(teserra_template_minimal_game "${template}/src/main.cpp")
target_link_libraries(teserra_template_minimal_game PRIVATE Teserra::Engine)
teserra_set_warnings(teserra_template_minimal_game)
teserra_check_public_includes(teserra_template_minimal_game)
teserra_copy_assets(teserra_template_minimal_game SOURCE_DIR "${template}/assets")

# Smoke run (docs/guides/testing-strategy.md#example-smoke-runs): 120 frames in a hidden window must exit with 0
# and log nothing at Error or above, so a missing hero.png fails it. Machines without an OpenGL 4.5 driver
# cannot create the window's context; the run is reported as skipped there.
add_test(NAME examples.template_minimal_game
         COMMAND teserra_template_minimal_game --frames 120 --hidden-window
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(examples.template_minimal_game PROPERTIES
    # "[error]" / "[critical]" level tags; [[] matches a literal '[' without escaping.
    FAIL_REGULAR_EXPRESSION "[[](error|critical)]"
    SKIP_REGULAR_EXPRESSION "OpenGL 4[.]5 core context"
    LABELS "examples"
)
