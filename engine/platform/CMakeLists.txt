teserra_add_module(platform LAYER 1
    DEPENDS core
    PUBLIC_HEADERS
        include/teserra/platform/event.hpp
        include/teserra/platform/opengl_context.hpp
        include/teserra/platform/platform.hpp
        include/teserra/platform/window.hpp
)

# Window backend, chosen at configure time; shared code never uses #ifdef to pick it
# (docs/architecture/platform-abstraction.md#selection-mechanism). GLFW is the only backend in v1.
set(TESERRA_WINDOW_BACKEND "GLFW" CACHE STRING "Window backend of teserra_platform")
set_property(CACHE TESERRA_WINDOW_BACKEND PROPERTY STRINGS GLFW)

if(TESERRA_WINDOW_BACKEND STREQUAL "GLFW")
    target_sources(teserra_platform PRIVATE
        src/glfw/glfw_platform.cpp
        src/glfw/glfw_window.cpp
    )
    if(WIN32)
        target_sources(teserra_platform PRIVATE src/glfw/glfw_native_windows.cpp)
    else()
        message(FATAL_ERROR "teserra_platform: the GLFW backend has no native-handle implementation for "
            "${CMAKE_SYSTEM_NAME} yet (see the new platform port checklist in platform-abstraction.md)")
    endif()
    # GLFW is PRIVATE (ADR-0006). GLFW_INCLUDE_NONE keeps <GLFW/glfw3.h> from including OpenGL headers:
    # GL belongs to the RHI's OpenGL backend.
    find_package(glfw3 CONFIG REQUIRED)
    teserra_link_third_party(platform PRIVATE glfw)
    target_compile_definitions(teserra_platform PRIVATE GLFW_INCLUDE_NONE)
else()
    message(FATAL_ERROR "teserra_platform: unknown TESERRA_WINDOW_BACKEND '${TESERRA_WINDOW_BACKEND}'")
endif()

if(TESERRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
