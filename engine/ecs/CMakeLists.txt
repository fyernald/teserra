teserra_add_module(ecs LAYER 4
    # Only core: the ECS knows nothing about rendering, assets or windows (docs/architecture/ecs.md).
    DEPENDS core
    PUBLIC_HEADERS
        include/teserra/ecs/entity.hpp
        include/teserra/ecs/world.hpp
    SOURCES
        src/world.cpp
)

if(TESERRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
