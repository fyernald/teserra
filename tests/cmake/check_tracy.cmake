# Checks that a built executable carries Tracy exactly when profiling is on (ADR-0023, #107): with
# TESERRA_ENABLE_PROFILING=OFF, no string of the Tracy client (its library name, its TRACY_* environment
# settings) may be in it. Run as: cmake -DFILE=<executable> -DEXPECT_TRACY=<ON|OFF> -P check_tracy.cmake
if(NOT EXISTS "${FILE}")
    message(FATAL_ERROR "No executable at '${FILE}'")
endif()
file(STRINGS "${FILE}" found REGEX "[Tt]racy[A-Z]|TRACY_" LIMIT_COUNT 5)
if(EXPECT_TRACY AND NOT found)
    message(FATAL_ERROR "'${FILE}' was built with profiling on but contains no Tracy strings")
elseif(NOT EXPECT_TRACY AND found)
    message(FATAL_ERROR "'${FILE}' was built with profiling off but contains Tracy strings: ${found}")
endif()
message(STATUS "Tracy strings in '${FILE}': ${found}")
