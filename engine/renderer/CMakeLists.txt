teserra_add_module(renderer LAYER 3
    # Only the abstract RHI: backends are chosen by teserra_engine (docs/architecture/overview.md).
    DEPENDS core rhi assets
    PUBLIC_HEADERS
        include/teserra/renderer/assets.hpp
        include/teserra/renderer/camera.hpp
        include/teserra/renderer/camera_data.hpp
        include/teserra/renderer/chunk_mesh_cache.hpp
        include/teserra/renderer/frame_packet.hpp
        include/teserra/renderer/render_pass.hpp
        include/teserra/renderer/renderer.hpp
        include/teserra/renderer/sprite_batcher.hpp
        include/teserra/renderer/sprite_pass.hpp
        include/teserra/renderer/sprite_sheet.hpp
        include/teserra/renderer/upload_ring.hpp
    SOURCES
        src/assets.cpp
        src/camera.cpp
        src/chunk_mesh_cache.cpp
        src/renderer.cpp
        src/sprite_batcher.cpp
        src/sprite_pass.cpp
        src/sprite_sheet.cpp
        src/upload_ring.cpp
)

# The built-in materials' shaders and the fallbacks of shader assets, compiled at build time and embedded:
# <teserra_renderer/shaders.hpp> declares teserra::renderer::shaders::kSpriteUnlitVert, kSpriteUnlitFrag,
# kErrorVert and kErrorFrag. shaders/ also holds the includes every engine shader uses (teserra/camera.glsl).
teserra_add_shaders(teserra_renderer EMBED
    NAMESPACE teserra::renderer::shaders
    HEADER teserra_renderer/shaders.hpp
    INCLUDE_DIRECTORIES shaders
    SOURCES
        shaders/error.vert
        shaders/error.frag
        shaders/sprite_unlit.vert
        shaders/sprite_unlit.frag
)

if(TESERRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
