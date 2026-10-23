#version 450
// Fails to compile unless the engine macros and the test's -D macros are as expected.
#if !defined(TESERRA_SHADER) || TESERRA_SHADER != 1
#error "TESERRA_SHADER is not 1"
#endif
#if !defined(TESERRA_STAGE_VERTEX) || defined(TESERRA_STAGE_FRAGMENT) || defined(TESERRA_STAGE_COMPUTE)
#error "only TESERRA_STAGE_VERTEX is defined in a vertex shader"
#endif
#if !defined(TESERRA_TEST_FLAG) || TESERRA_TEST_FLAG != 1
#error "-D TESERRA_TEST_FLAG defines it to 1"
#endif
#if TESERRA_TEST_VALUE != 42
#error "-D TESERRA_TEST_VALUE=42 defines it to 42"
#endif

void main() {
    gl_Position = vec4(0.0);
}
