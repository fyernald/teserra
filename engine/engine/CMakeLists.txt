teserra_add_module(engine LAYER 7
    # The RHI backends the build includes; App passes them to rhi::createDevice(). The null backend is
    # always built. The renderer draws the active scene; assets provides the mount points. The debug UI
    # exists with TESERRA_WITH_DEBUG_UI.
    DEPENDS core platform rhi rhi_null assets audio renderer ecs scene
    OPTIONAL_DEPENDS rhi_opengl debug_ui
    PUBLIC_HEADERS
        include/teserra/engine/app.hpp
        include/teserra/teserra.hpp
    SOURCES
        src/app.cpp
)

# Public, so that games can wrap their own debug windows in #if TESERRA_WITH_DEBUG_UI and build both ways.
if(TARGET teserra_debug_ui)
    target_compile_definitions(teserra_engine PUBLIC TESERRA_WITH_DEBUG_UI=1)
else()
    target_compile_definitions(teserra_engine PUBLIC TESERRA_WITH_DEBUG_UI=0)
endif()

if(TESERRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
