teserra_add_test(rhi_opengl
    SOURCES
        gl_debug_test.cpp
        opengl_device_test.cpp
        opengl_gpu_test.cpp
        opengl_swapchain_test.cpp
    DEPENDS rhi_opengl platform
    # The fake driver and the GPU test call GL themselves.
    LIBRARIES teserra_glad
)
# Some tests use the backend's private headers (the debug-message mapping, formats, the device).
target_include_directories(teserra_rhi_opengl_tests PRIVATE ../src)

# The public header compiles on its own and keeps GL and glad out (ADR-0009).
teserra_add_header_check(rhi_opengl)
