set(scene_render_tests "")
if(TARGET teserra_renderer)
    set(scene_render_tests render_extraction_test.cpp sprite_sheet_sprites_test.cpp tilemap_render_test.cpp tiled_scene_test.cpp tileset_test.cpp)
endif()

teserra_add_test(scene
    SOURCES
        collision_test.cpp
        hierarchy_test.cpp
        scene_systems_test.cpp
        scene_test.cpp
        tile_queries_test.cpp
        tilemap_test.cpp
        transform_test.cpp
        ${scene_render_tests}
    # The null backend gives the sprite sheet tests a device for their texture assets.
    DEPENDS scene rhi_null
)

# Every public header compiles on its own.
teserra_add_header_check(scene)
