# The "dream code" of docs/architecture/public-api.md, compiled (#119): the first ```cpp block of that page
# becomes public_api_example.cpp in the build tree, so a change that breaks the page's example breaks the
# build. Editing the page reconfigures.
set(page "${PROJECT_SOURCE_DIR}/docs/architecture/public-api.md")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${page}")
file(READ "${page}" markdown)
# Checkouts with CRLF line endings (Git on Windows) read the same as LF ones.
string(REPLACE "\r" "" markdown "${markdown}")
string(FIND "${markdown}" "```cpp\n" start)
if(start EQUAL -1)
    message(FATAL_ERROR "${page} has no ```cpp block for the public API example")
endif()
math(EXPR start "${start} + 7")
string(SUBSTRING "${markdown}" ${start} -1 rest)
string(FIND "${rest}" "\n```" length)
string(SUBSTRING "${rest}" 0 ${length} code)
set(example "${CMAKE_CURRENT_BINARY_DIR}/public_api_example.cpp")
file(CONFIGURE OUTPUT "${example}" CONTENT "${code}\n" @ONLY)

add_executable(teserra_public_api_example "${example}")
target_link_libraries(teserra_public_api_example PRIVATE Teserra::Engine)
teserra_set_warnings(teserra_public_api_example)
