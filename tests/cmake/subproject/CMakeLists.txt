# Fixture project that consumes Teserra through add_subdirectory(), like a game would. Configured by
# the cmake.subproject.* test in tests/cmake/CMakeLists.txt.

cmake_minimum_required(VERSION 3.28)
project(TeserraSubprojectFixture LANGUAGES CXX)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../.." teserra)

foreach(option IN ITEMS TESERRA_BUILD_TESTS TESERRA_BUILD_EXAMPLES)
    if(${option})
        message(FATAL_ERROR "Fixture: ${option} must default to OFF for a subproject")
    endif()
endforeach()
foreach(target IN ITEMS teserra_core_tests teserra_hello)
    if(TARGET ${target})
        message(FATAL_ERROR "Fixture: '${target}' must not be defined for a subproject")
    endif()
endforeach()
if(NOT TARGET Teserra::Core)
    message(FATAL_ERROR "Fixture: Teserra::Core is not available to the parent project")
endif()

message(STATUS "Fixture: Teserra consumed as a subproject with tests and examples off")
