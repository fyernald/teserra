teserra_add_test(engine
    SOURCES
        app_test.cpp
        app_window_test.cpp
    DEPENDS engine
)
target_include_directories(teserra_engine_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# The tests of the OpenGL device path need to know whether the engine includes that backend.
target_compile_definitions(teserra_engine_tests PRIVATE
    "TESERRA_HAS_RHI_OPENGL=$<TARGET_EXISTS:teserra_rhi_opengl>"
)

teserra_add_header_check(engine)
