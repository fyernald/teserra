set(platform_test_sources
    clock_test.cpp
    event_queue_test.cpp
    file_system_test.cpp
    fixed_step_accumulator_test.cpp
    frame_timer_test.cpp
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
# The file system test looks for its own executable in executableDirectory().
target_compile_definitions(teserra_platform_tests PRIVATE
    TESERRA_TEST_EXECUTABLE_NAME="$<TARGET_FILE_NAME:teserra_platform_tests>")

# Every public header compiles on its own and keeps private dependencies (GLFW, <windows.h>) out.
teserra_add_header_check(platform)
