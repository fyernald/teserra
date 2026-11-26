# The coin collector of docs/guides/getting-started.md, compiled and run (#121): the ```cpp block after the
# page's "tests/getting_started compiles the next block" comment becomes getting_started.cpp in the build tree,
# so a change that breaks the guide's program breaks the build. Editing the page reconfigures.
set(page "${PROJECT_SOURCE_DIR}/docs/guides/getting-started.md")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${page}")
file(READ "${page}" markdown)
# Checkouts with CRLF line endings (Git on Windows) read the same as LF ones.
string(REPLACE "\r" "" markdown "${markdown}")
string(FIND "${markdown}" "<!-- tests/getting_started compiles the next block" marker)
if(marker EQUAL -1)
    message(FATAL_ERROR "${page} has no \"tests/getting_started compiles the next block\" comment")
endif()
string(SUBSTRING "${markdown}" ${marker} -1 rest)
string(FIND "${rest}" "```cpp\n" start)
if(start EQUAL -1)
    message(FATAL_ERROR "${page} has no ```cpp block after the tests/getting_started comment")
endif()
math(EXPR start "${start} + 7")
string(SUBSTRING "${rest}" ${start} -1 rest)
string(FIND "${rest}" "\n```" length)
string(SUBSTRING "${rest}" 0 ${length} code)
set(program "${CMAKE_CURRENT_BINARY_DIR}/getting_started.cpp")
file(CONFIGURE OUTPUT "${program}" CONTENT "${code}\n" @ONLY)

add_executable(teserra_getting_started "${program}")
target_link_libraries(teserra_getting_started PRIVATE Teserra::Engine)
teserra_set_warnings(teserra_getting_started)

# The guide's assets: the template's hero, which also stands in for the coin, as the guide suggests.
set(template_hero "${PROJECT_SOURCE_DIR}/templates/minimal_game/assets/hero.png")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${template_hero}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/assets_source")
file(COPY_FILE "${template_hero}" "${CMAKE_CURRENT_BINARY_DIR}/assets_source/hero.png" ONLY_IF_DIFFERENT)
file(COPY_FILE "${template_hero}" "${CMAKE_CURRENT_BINARY_DIR}/assets_source/coin.png" ONLY_IF_DIFFERENT)
teserra_copy_assets(teserra_getting_started SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/assets_source")

# Smoke run (docs/guides/testing-strategy.md#example-smoke-runs): 120 frames in a hidden window must exit with 0
# and log nothing at Error or above. Machines without an OpenGL 4.5 driver cannot create the window's context;
# the run is reported as skipped there.
add_test(NAME examples.getting_started
         COMMAND teserra_getting_started --frames 120 --hidden-window
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(examples.getting_started PROPERTIES
    # "[error]" / "[critical]" level tags; [[] matches a literal '[' without escaping.
    FAIL_REGULAR_EXPRESSION "[[](error|critical)]"
    SKIP_REGULAR_EXPRESSION "${TESERRA_SMOKE_SKIP_REGEX}"
    LABELS "examples"
)
