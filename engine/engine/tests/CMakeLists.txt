teserra_add_test(engine
    SOURCES
        app_assets_test.cpp
        app_render_target_gpu_test.cpp
        app_render_test.cpp
        app_scene_render_test.cpp
        app_scene_test.cpp
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

# Shaders of the GPU tests, compiled and embedded: <engine_tests/shaders.hpp> declares
# teserra::test::shaders::kTintVert and kTintFrag (a sprite material). The renderer's shaders/ holds the
# engine's shader includes (teserra/camera.glsl).
teserra_add_shaders(teserra_engine_tests EMBED
    NAMESPACE teserra::test::shaders
    HEADER engine_tests/shaders.hpp
    INCLUDE_DIRECTORIES ../../renderer/shaders
    SOURCES
        shaders/tint.vert
        shaders/tint.frag
)
