add_executable(teserra_rhi_clear main.cpp)
target_link_libraries(teserra_rhi_clear PRIVATE Teserra::Engine)
teserra_set_warnings(teserra_rhi_clear)

# Smoke runs (docs/guides/testing-strategy.md#example-smoke-runs): 60 frames, also with the debug UI open.
if(TESERRA_BUILD_TESTS)
    teserra_add_smoke_run(rhi_clear TARGET teserra_rhi_clear FRAMES 60)
endif()
