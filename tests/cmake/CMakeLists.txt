# Tests of the CMake helpers themselves. Each case configures the fixture project in layer_rule/ in a
# separate build directory and checks the outcome.

function(_teserra_add_layer_rule_test case)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "EXPECT_OUTPUT" "")
    set(name "cmake.layer_rule.${case}")
    add_test(NAME ${name}
        COMMAND "${CMAKE_COMMAND}"
            -S "${CMAKE_CURRENT_SOURCE_DIR}/layer_rule"
            -B "${CMAKE_CURRENT_BINARY_DIR}/layer_rule_${case}"
            -G "${CMAKE_GENERATOR}"
            "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DTESERRA_FIXTURE_CASE=${case}"
    )
    if(arg_EXPECT_OUTPUT)
        # The expected message only appears when configuration failed, so matching it is enough.
        # CMake wraps long messages, so any space in the expectation also matches a line break.
        string(REPLACE " " "[ \t\r\n]+" regex "${arg_EXPECT_OUTPUT}")
        set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "${regex}")
    endif()
    set_tests_properties(${name} PROPERTIES LABELS "cmake")
endfunction()

_teserra_add_layer_rule_test(accepts_lower_layers)
_teserra_add_layer_rule_test(rejects_higher_layer
    EXPECT_OUTPUT "Layer violation: module 'core' \\(layer 0\\) must not depend on module 'platform' \\(layer 1\\)"
)
_teserra_add_layer_rule_test(rejects_same_layer
    EXPECT_OUTPUT "Layer violation: module 'assets' \\(layer 2\\) must not depend on module 'rhi' \\(layer 2\\)"
)
_teserra_add_layer_rule_test(rejects_unknown_module
    EXPECT_OUTPUT "unknown module 'platform' in DEPENDS"
)
