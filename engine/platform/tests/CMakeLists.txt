set(platform_test_sources
    event_queue_test.cpp
    fixed_step_accumulator_test.cpp
    frame_timer_test.cpp
    input_test.cpp
    key_test.cpp
)
# The native file system, wherever the OS services are built (also in the portable build on Linux).
if(TESERRA_PLATFORM_OS_SERVICES)
    list(APPEND platform_test_sources file_system_test.cpp)
endif()
# These need a Platform, which the portable build (#68) does not have.
if(NOT TESERRA_PORTABLE_ONLY)
    list(APPEND platform_test_sources clock_test.cpp platform_test.cpp window_test.cpp)
endif()
# OS-specific tests are selected here, like OS-specific sources (no #ifdef in shared code).
if(WIN32)
    list(APPEND platform_test_sources windows/window_events_windows_test.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND platform_test_sources posix/file_system_posix_test.cpp)
endif()

teserra_add_test(platform
    SOURCES ${platform_test_sources}
    DEPENDS platform
)
target_include_directories(teserra_platform_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# The file system test looks for its own executable in executableDirectory().
target_compile_definitions(teserra_platform_tests PRIVATE
    TESERRA_TEST_EXECUTABLE_NAME="$<TARGET_FILE_NAME:teserra_platform_tests>")

# Every public header compiles on its own and keeps private dependencies (GLFW, <windows.h>) out.
teserra_add_header_check(platform)
