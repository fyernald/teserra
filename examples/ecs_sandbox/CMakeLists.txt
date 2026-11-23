add_executable(teserra_ecs_sandbox main.cpp)
target_link_libraries(teserra_ecs_sandbox PRIVATE Teserra::Ecs)
teserra_set_warnings(teserra_ecs_sandbox)

# Smoke run: 100 000 entities must exit with 0 and log nothing at Error or above
# (docs/guides/testing-strategy.md#example-smoke-runs).
if(TESERRA_BUILD_TESTS)
    add_test(NAME examples.ecs_sandbox COMMAND teserra_ecs_sandbox)
    set_tests_properties(examples.ecs_sandbox PROPERTIES
        # "[error]" / "[critical]" level tags; [[] matches a literal '[' without escaping.
        FAIL_REGULAR_EXPRESSION "[[](error|critical)]"
        LABELS "examples"
    )
endif()
