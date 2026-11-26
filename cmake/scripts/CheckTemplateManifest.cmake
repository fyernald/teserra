# Checks that templates/minimal_game builds with the libraries and versions Teserra is built with (#120):
# its vcpkg.json lists exactly the dependencies of Teserra's vcpkg.json plus those of its default features and
# of the features a game builds with by default (debug-ui: TESERRA_WITH_DEBUG_UI is ON in a game too),
# and its vcpkg-configuration.json equals Teserra's.
#
#   cmake -DTESERRA_SOURCE_DIR=<repo> -P CheckTemplateManifest.cmake

if(NOT DEFINED TESERRA_SOURCE_DIR)
    message(FATAL_ERROR "CheckTemplateManifest: pass -DTESERRA_SOURCE_DIR=<repository root>")
endif()
set(template "${TESERRA_SOURCE_DIR}/templates/minimal_game")

# The names in the JSON array at <member path> of <json>; an entry is a name or an object with "name".
function(_teserra_dependency_names out json)
    set(names "")
    string(JSON count ERROR_VARIABLE error LENGTH "${json}" ${ARGN})
    if(error)
        set(${out} "" PARENT_SCOPE)
        return()
    endif()
    if(count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach(i RANGE ${last})
            string(JSON type TYPE "${json}" ${ARGN} ${i})
            if(type STREQUAL "OBJECT")
                string(JSON name GET "${json}" ${ARGN} ${i} name)
            else()
                string(JSON name GET "${json}" ${ARGN} ${i})
            endif()
            list(APPEND names "${name}")
        endforeach()
    endif()
    set(${out} "${names}" PARENT_SCOPE)
endfunction()

file(READ "${TESERRA_SOURCE_DIR}/vcpkg.json" engine_manifest)
file(READ "${template}/vcpkg.json" template_manifest)

_teserra_dependency_names(expected "${engine_manifest}" dependencies)
_teserra_dependency_names(default_features "${engine_manifest}" default-features)
list(APPEND default_features debug-ui)
foreach(feature IN LISTS default_features)
    _teserra_dependency_names(feature_dependencies "${engine_manifest}" features ${feature} dependencies)
    list(APPEND expected ${feature_dependencies})
endforeach()
_teserra_dependency_names(actual "${template_manifest}" dependencies)
list(REMOVE_DUPLICATES expected)
list(SORT expected)
list(SORT actual)
if(NOT actual STREQUAL expected)
    message(FATAL_ERROR
        "templates/minimal_game/vcpkg.json lists '${actual}', but Teserra builds with '${expected}' "
        "(vcpkg.json: dependencies, the default features and debug-ui). Update the template's dependencies.")
endif()

file(READ "${TESERRA_SOURCE_DIR}/vcpkg-configuration.json" engine_configuration)
file(READ "${template}/vcpkg-configuration.json" template_configuration)
string(JSON same EQUAL "${engine_configuration}" "${template_configuration}")
if(NOT same)
    message(FATAL_ERROR
        "templates/minimal_game/vcpkg-configuration.json differs from Teserra's vcpkg-configuration.json: "
        "copy it over, so the game builds with the library versions Teserra is tested with.")
endif()

message(STATUS "The template's vcpkg manifest matches Teserra's: ${actual}")
