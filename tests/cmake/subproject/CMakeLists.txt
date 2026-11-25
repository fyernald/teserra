# Fixture project that consumes Teserra through add_subdirectory(), like a game would. Configured by
# the cmake.subproject.* tests in tests/cmake/CMakeLists.txt.

cmake_minimum_required(VERSION 3.28)
project(TeserraSubprojectFixture LANGUAGES CXX)

# Everything Teserra could change for the parent project if it set it globally (#120).
set(_fixture_variables
    CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_EXE_LINKER_FLAGS
    CMAKE_EXE_LINKER_FLAGS_DEBUG CMAKE_CXX_STANDARD CMAKE_CXX_EXTENSIONS CMAKE_CXX_SCAN_FOR_MODULES
    CMAKE_MODULE_PATH CMAKE_RUNTIME_OUTPUT_DIRECTORY VCPKG_MANIFEST_FEATURES)
set(_fixture_directory_properties COMPILE_OPTIONS COMPILE_DEFINITIONS LINK_OPTIONS INCLUDE_DIRECTORIES)
foreach(variable IN LISTS _fixture_variables)
    set(_fixture_before_${variable} "${${variable}}")
endforeach()
foreach(property IN LISTS _fixture_directory_properties)
    get_directory_property(_fixture_before_${property} ${property})
endforeach()

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
foreach(target IN ITEMS Teserra::Core Teserra::Engine)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "Fixture: ${target} is not available to the parent project")
    endif()
endforeach()
foreach(command IN ITEMS teserra_copy_assets teserra_add_shaders teserra_set_warnings)
    if(NOT COMMAND ${command})
        message(FATAL_ERROR "Fixture: ${command}() is not available to the parent project")
    endif()
endforeach()

# Teserra's options and flags stay inside Teserra: the parent's variables and directory properties are
# unchanged, and no engine target passes compile or link options on to the targets that link it. Warnings,
# warnings as errors and sanitizer flags are PRIVATE or directory-scoped inside Teserra.
foreach(variable IN LISTS _fixture_variables)
    if(NOT "${${variable}}" STREQUAL "${_fixture_before_${variable}}")
        message(FATAL_ERROR
            "Fixture: Teserra changed the parent's ${variable} from '${_fixture_before_${variable}}' to '${${variable}}'")
    endif()
endforeach()
foreach(property IN LISTS _fixture_directory_properties)
    get_directory_property(after ${property})
    if(NOT "${after}" STREQUAL "${_fixture_before_${property}}")
        message(FATAL_ERROR
            "Fixture: Teserra changed the parent directory's ${property} from '${_fixture_before_${property}}' to '${after}'")
    endif()
endforeach()
foreach(module IN ITEMS core platform rhi rhi_null rhi_opengl assets renderer ecs scene engine)
    if(NOT TARGET teserra_${module})
        continue()
    endif()
    foreach(property IN ITEMS INTERFACE_COMPILE_OPTIONS INTERFACE_LINK_OPTIONS)
        get_target_property(value teserra_${module} ${property})
        if(value)
            message(FATAL_ERROR "Fixture: teserra_${module} passes ${property} '${value}' on to the game")
        endif()
    endforeach()
endforeach()

message(STATUS "Fixture: Teserra consumed as a subproject with tests and examples off and nothing leaked")
