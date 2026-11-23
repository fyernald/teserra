teserra_add_module(scene LAYER 5
    # Scenes own worlds (docs/architecture/ecs.md#scenes-own-worlds). No platform or GPU code, so the portable
    # Linux build includes it.
    DEPENDS core ecs
    PUBLIC_HEADERS
        include/teserra/scene/scene.hpp
        include/teserra/scene/scene_manager.hpp
        include/teserra/scene/time.hpp
    SOURCES
        src/scene.cpp
)

if(TESERRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
