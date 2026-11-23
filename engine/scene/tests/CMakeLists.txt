set(scene_render_tests "")
if(TARGET teserra_renderer)
    set(scene_render_tests render_extraction_test.cpp)
endif()

teserra_add_test(scene
    SOURCES
        scene_systems_test.cpp
        scene_test.cpp
        transform_test.cpp
        ${scene_render_tests}
    DEPENDS scene
)

# Every public header compiles on its own.
teserra_add_header_check(scene)
