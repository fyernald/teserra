set(platform_test_sources
    event_queue_test.cpp
    input_test.cpp
    key_test.cpp
    window_test.cpp
)
# OS-specific tests are selected here, like OS-specific sources (no #ifdef in shared code).
if(WIN32)
    list(APPEND platform_test_sources windows/window_events_windows_test.cpp)
endif()

teserra_add_test(platform
    SOURCES ${platform_test_sources}
    DEPENDS platform
)
target_include_directories(teserra_platform_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# Every public header compiles on its own and keeps private dependencies (GLFW, <windows.h>) out.
teserra_add_header_check(platform)
