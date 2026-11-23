teserra_add_test(ecs
    SOURCES
        world_test.cpp
    DEPENDS ecs
)

# Every public header compiles on its own.
teserra_add_header_check(ecs)
