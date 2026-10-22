add_executable(teserra_rhi_clear main.cpp)
target_link_libraries(teserra_rhi_clear PRIVATE Teserra::Engine)
teserra_set_warnings(teserra_rhi_clear)

# Smoke run (docs/guides/testing-strategy.md#example-smoke-runs): 60 frames in a hidden window must exit with
# 0 and log nothing at Error or above, which includes OpenGL debug errors. Machines without an OpenGL 4.5
# driver (the CI runners) cannot create the window's context; the run is reported as skipped there.
if(TESERRA_BUILD_TESTS)
    add_test(NAME examples.rhi_clear COMMAND teserra_rhi_clear --frames 60 --hidden-window)
    set_tests_properties(examples.rhi_clear PROPERTIES
        # "[error]" / "[critical]" level tags; [[] matches a literal '[' without escaping.
        FAIL_REGULAR_EXPRESSION "[[](error|critical)]"
        SKIP_REGULAR_EXPRESSION "OpenGL 4[.]5 core context"
        LABELS "examples"
    )
endif()
