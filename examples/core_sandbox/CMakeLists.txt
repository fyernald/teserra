add_executable(teserra_core_sandbox main.cpp)
target_link_libraries(teserra_core_sandbox PRIVATE Teserra::Core)
teserra_set_warnings(teserra_core_sandbox)

# Smoke run: the default run must exit with 0 and log nothing at Error or above
# (docs/guides/testing-strategy.md#example-smoke-runs).
if(TESERRA_BUILD_TESTS)
    add_test(NAME examples.core_sandbox COMMAND teserra_core_sandbox)
    set_tests_properties(examples.core_sandbox PROPERTIES
        # "[error]" / "[critical]" level tags; [[] matches a literal '[' without escaping.
        FAIL_REGULAR_EXPRESSION "[[](error|critical)]"
        LABELS "examples"
    )
endif()
