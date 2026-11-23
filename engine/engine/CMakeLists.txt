teserra_add_module(engine LAYER 6
    # The RHI backends the build includes; App passes them to rhi::createDevice(). The null backend is
    # always built. The renderer draws the active scene.
    DEPENDS core platform rhi rhi_null renderer ecs scene
    OPTIONAL_DEPENDS rhi_opengl
    PUBLIC_HEADERS
        include/teserra/engine/app.hpp
    SOURCES
        src/app.cpp
)

if(TESERRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
