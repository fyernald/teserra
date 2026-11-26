add_executable(teserra_demo_game main.cpp art.cpp art.hpp flow.cpp flow.hpp font.cpp font.hpp gameplay.cpp gameplay.hpp)
target_link_libraries(teserra_demo_game PRIVATE Teserra::Engine)
teserra_set_warnings(teserra_demo_game)
# The game is dogfooding: it may use only the public API.
teserra_check_public_includes(teserra_demo_game)

# Smoke runs (docs/guides/testing-strategy.md#example-smoke-runs): 600 frames, also with the debug UI open.
if(TESERRA_BUILD_TESTS)
    teserra_add_smoke_run(demo_game TARGET teserra_demo_game FRAMES 600)
endif()
