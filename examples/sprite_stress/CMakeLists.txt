add_executable(teserra_sprite_stress main.cpp)
target_link_libraries(teserra_sprite_stress PRIVATE Teserra::Engine Teserra::Renderer)
teserra_set_warnings(teserra_sprite_stress)

# Smoke runs (docs/guides/testing-strategy.md#example-smoke-runs): 600 frames, also with the debug UI open.
# The default 1000 sprites; 10 000 take a minute in a Debug build, so run `--sprites 10000` by hand.
if(TESERRA_BUILD_TESTS)
    teserra_add_smoke_run(sprite_stress TARGET teserra_sprite_stress FRAMES 600)
endif()
