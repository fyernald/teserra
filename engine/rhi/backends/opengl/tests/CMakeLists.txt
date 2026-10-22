teserra_add_test(rhi_opengl
    SOURCES
        gl_debug_test.cpp
        opengl_device_test.cpp
        opengl_gpu_test.cpp
    DEPENDS rhi_opengl platform
    # The fake driver and the GPU test call GL themselves.
    LIBRARIES teserra_glad
)
# gl_debug_test.cpp tests the backend's private debug-message mapping.
target_include_directories(teserra_rhi_opengl_tests PRIVATE ../src)

# The public header compiles on its own and keeps GL and glad out (ADR-0009).
teserra_add_header_check(rhi_opengl)
