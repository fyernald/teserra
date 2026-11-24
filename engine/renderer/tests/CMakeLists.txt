teserra_add_test(renderer
    SOURCES
        assets_test.cpp
        camera_test.cpp
        chunk_mesh_cache_test.cpp
        debug_draw_test.cpp
        renderer_test.cpp
        sprite_sheet_file_test.cpp
        sprite_sheet_test.cpp
        sprite_batcher_test.cpp
        sprite_pass_test.cpp
        upload_ring_test.cpp
    # The null backend runs the renderer without a GPU (docs/guides/testing-strategy.md#rhi-and-renderer-tests).
    DEPENDS renderer rhi_null
)

# Shaders of the tests, compiled and embedded: <renderer_tests/shaders.hpp> declares
# teserra::test::shaders::kCameraBlockVert. ../shaders holds the engine's shader includes (teserra/camera.glsl).
teserra_add_shaders(teserra_renderer_tests EMBED
    NAMESPACE teserra::test::shaders
    HEADER renderer_tests/shaders.hpp
    INCLUDE_DIRECTORIES ../shaders
    SOURCES
        shaders/camera_block.vert
)

# Every public header compiles on its own.
teserra_add_header_check(renderer)
