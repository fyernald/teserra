add_executable(teserra_window_input main.cpp)
target_link_libraries(teserra_window_input PRIVATE Teserra::Engine)
teserra_set_warnings(teserra_window_input)

# Smoke runs (docs/guides/testing-strategy.md#example-smoke-runs): 120 frames, also with the debug UI open.
if(TESERRA_BUILD_TESTS)
    teserra_add_smoke_run(window_input TARGET teserra_window_input FRAMES 120)
endif()
