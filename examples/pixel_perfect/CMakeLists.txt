add_executable(teserra_pixel_perfect main.cpp)
target_link_libraries(teserra_pixel_perfect PRIVATE Teserra::Engine)
teserra_set_warnings(teserra_pixel_perfect)

# Smoke runs (docs/guides/testing-strategy.md#example-smoke-runs): 120 frames, also with the debug UI open.
if(TESERRA_BUILD_TESTS)
    teserra_add_smoke_run(pixel_perfect TARGET teserra_pixel_perfect FRAMES 120)
endif()
