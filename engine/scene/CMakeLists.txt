# Render extraction (SpriteRenderer, Camera, RenderExtraction) needs the renderer, which the portable build
# leaves out; the rest of the module is built there too.
set(scene_render_headers "")
set(scene_render_sources "")
if(TARGET teserra_renderer)
    set(scene_render_headers
        include/teserra/scene/collision_debug.hpp
        include/teserra/scene/render_components.hpp
        include/teserra/scene/render_extraction.hpp
        include/teserra/scene/tiled.hpp
        include/teserra/scene/tileset.hpp
    )
    set(scene_render_sources src/collision_debug.cpp src/render_components.cpp src/render_extraction.cpp src/tiled.cpp src/tileset.cpp)
endif()

# AudioSource and AudioListener need the audio module, which the portable build leaves out too.
set(scene_audio_headers "")
set(scene_audio_sources "")
if(TARGET teserra_audio)
    set(scene_audio_headers include/teserra/scene/audio_components.hpp)
    set(scene_audio_sources src/audio_system.cpp)
endif()

teserra_add_module(scene LAYER 5
    # Scenes own worlds (docs/architecture/ecs.md#scenes-own-worlds). No platform or GPU code, so the portable
    # Linux build includes it; with the renderer, scenes also extract render data (TESERRA_HAS_RENDERER), and
    # with audio they play AudioSources (TESERRA_HAS_AUDIO).
    DEPENDS core rhi ecs
    OPTIONAL_DEPENDS renderer assets audio
    PUBLIC_HEADERS
        include/teserra/scene/collision.hpp
        include/teserra/scene/hierarchy.hpp
        include/teserra/scene/scene.hpp
        include/teserra/scene/scene_manager.hpp
        include/teserra/scene/tile_queries.hpp
        include/teserra/scene/tilemap.hpp
        include/teserra/scene/time.hpp
        include/teserra/scene/transform.hpp
        ${scene_render_headers}
        ${scene_audio_headers}
    SOURCES
        src/collision.cpp
        src/hierarchy.cpp
        src/scene.cpp
        src/tile_queries.cpp
        src/tilemap.cpp
        src/transform.cpp
        ${scene_render_sources}
        ${scene_audio_sources}
)

if(TESERRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
