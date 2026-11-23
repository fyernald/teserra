teserra_add_test(scene
    SOURCES
        scene_systems_test.cpp
        scene_test.cpp
        transform_test.cpp
    DEPENDS scene
)

# Every public header compiles on its own.
teserra_add_header_check(scene)
