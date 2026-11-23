teserra_add_test(ecs
    SOURCES
        commands_test.cpp
        component_test.cpp
        resource_test.cpp
        view_test.cpp
        world_test.cpp
    DEPENDS ecs
)

# Every public header compiles on its own.
teserra_add_header_check(ecs)
