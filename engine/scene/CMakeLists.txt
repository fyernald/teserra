# Render extraction (SpriteRenderer, Camera, RenderExtraction) needs the renderer, which the portable build
# leaves out; the rest of the module is built there too.
set(scene_render_headers "")
set(scene_render_sources "")
if(TARGET teserra_renderer)
    set(scene_render_headers
        include/teserra/scene/render_components.hpp
        include/teserra/scene/render_extraction.hpp
        include/teserra/scene/tileset.hpp
    )
    set(scene_render_sources src/render_components.cpp src/render_extraction.cpp src/tileset.cpp)
endif()

teserra_add_module(scene LAYER 5
    # Scenes own worlds (docs/architecture/ecs.md#scenes-own-worlds). No platform or GPU code, so the portable
    # Linux build includes it; with the renderer, scenes also extract render data (TESERRA_HAS_RENDERER).
    DEPENDS core rhi ecs
    OPTIONAL_DEPENDS renderer assets
    PUBLIC_HEADERS
        include/teserra/scene/scene.hpp
        include/teserra/scene/scene_manager.hpp
        include/teserra/scene/tilemap.hpp
        include/teserra/scene/time.hpp
        include/teserra/scene/transform.hpp
        ${scene_render_headers}
    SOURCES
        src/scene.cpp
        src/tilemap.cpp
        src/transform.cpp
        ${scene_render_sources}
)

if(TESERRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
