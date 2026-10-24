teserra_add_module(platform LAYER 1
    DEPENDS core
    PUBLIC_HEADERS
        include/teserra/platform/clock.hpp
        include/teserra/platform/event.hpp
        include/teserra/platform/file_system.hpp
        include/teserra/platform/fixed_step_accumulator.hpp
        include/teserra/platform/frame_timer.hpp
        include/teserra/platform/input.hpp
        include/teserra/platform/key.hpp
        include/teserra/platform/mouse_button.hpp
        include/teserra/platform/opengl_context.hpp
        include/teserra/platform/platform.hpp
        include/teserra/platform/window.hpp
)

# Portable implementation, shared by every backend.
target_sources(teserra_platform PRIVATE
    src/common/clock.cpp
    src/common/fixed_step_accumulator.cpp
    src/common/frame_timer.cpp
    src/common/input.cpp
    src/common/key.cpp
    src/common/mouse_button.cpp
    src/common/native_file_system.cpp
)

# OS services: the parts of the file system only the OS knows (executable and user-data directories).
if(WIN32)
    target_sources(teserra_platform PRIVATE src/windows/file_system_windows.cpp)
    # SHGetKnownFolderPath (shell32), CoTaskMemFree (ole32) and the FOLDERID_* GUIDs (uuid).
    target_link_libraries(teserra_platform PRIVATE shell32 ole32 uuid)
else()
    message(FATAL_ERROR "teserra_platform: no OS services for ${CMAKE_SYSTEM_NAME} yet (see the new "
        "platform port checklist in platform-abstraction.md)")
endif()

# Window backend, chosen at configure time; shared code never uses #ifdef to pick it
# (docs/architecture/platform-abstraction.md#selection-mechanism). GLFW is the only backend in v1.
set(TESERRA_WINDOW_BACKEND "GLFW" CACHE STRING "Window backend of teserra_platform")
set_property(CACHE TESERRA_WINDOW_BACKEND PROPERTY STRINGS GLFW)

if(TESERRA_WINDOW_BACKEND STREQUAL "GLFW")
    target_sources(teserra_platform PRIVATE
        src/glfw/glfw_key_table.cpp
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
