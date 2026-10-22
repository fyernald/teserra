# Engine module helpers. See docs/architecture/overview.md#how-the-layering-is-enforced.
#
#   teserra_add_module(<name> LAYER <n>
#       [DEPENDS <module>...]
#       [OPTIONAL_DEPENDS <module>...]
#       [PUBLIC_HEADERS <file>...]
#       [SOURCES <file>...])
#
# Creates the static library teserra_<name> with the alias Teserra::<Name> (ADR-0021), compiled as
# C++20 without extensions and with the Teserra warning set. include/ is the public include directory
# (as the HEADERS file set), src/ the private one. Every module in DEPENDS is linked PUBLIC and must
# already exist in a strictly lower layer; otherwise configuration fails.
#
# OPTIONAL_DEPENDS names modules that an option may leave out of the build, such as RHI backends. One
# that exists is linked and checked like DEPENDS; the module's sources see TESERRA_HAS_<MODULE> (the
# name in upper case) defined to 1 if it exists and 0 if not.
#
#   teserra_link_third_party(<name> <PUBLIC|PRIVATE|INTERFACE> <target>...)
#
# Links third-party targets to module <name>. The visibility keyword is mandatory so that every
# dependency that leaks into a module's public interface is an explicit, reviewed choice.

include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/TeserraWarnings.cmake")

# Turns a snake_case module name into PascalCase for the alias: rhi_null -> RhiNull.
function(_teserra_pascal_case out name)
    string(REPLACE "_" ";" words "${name}")
    set(result "")
    foreach(word IN LISTS words)
        string(SUBSTRING "${word}" 0 1 first)
        string(SUBSTRING "${word}" 1 -1 rest)
        string(TOUPPER "${first}" first)
        string(APPEND result "${first}${rest}")
    endforeach()
    set(${out} "${result}" PARENT_SCOPE)
endfunction()

function(teserra_add_module name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "LAYER" "DEPENDS;OPTIONAL_DEPENDS;PUBLIC_HEADERS;SOURCES")

    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "teserra_add_module(${name}): unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT name MATCHES "^[a-z][a-z0-9_]*$")
        message(FATAL_ERROR "teserra_add_module(${name}): module names are lower_snake_case")
    endif()
    if(NOT DEFINED arg_LAYER OR NOT arg_LAYER MATCHES "^[0-9]+$")
        message(FATAL_ERROR "teserra_add_module(${name}): LAYER <n> with a non-negative integer is required")
    endif()

    get_property(already_defined GLOBAL PROPERTY TESERRA_MODULE_LAYER_${name} SET)
    if(already_defined)
        message(FATAL_ERROR "teserra_add_module(${name}): module '${name}' is already defined")
    endif()

    # Optional dependencies that exist count as dependencies from here on.
    set(optional_definitions "")
    foreach(dependency IN LISTS arg_OPTIONAL_DEPENDS)
        if(NOT dependency MATCHES "^[a-z][a-z0-9_]*$")
            message(FATAL_ERROR "teserra_add_module(${name}): '${dependency}' in OPTIONAL_DEPENDS is not a module name")
        endif()
        string(TOUPPER "${dependency}" upper_dependency)
        get_property(dependency_known GLOBAL PROPERTY TESERRA_MODULE_LAYER_${dependency} SET)
        if(dependency_known)
            list(APPEND arg_DEPENDS ${dependency})
            list(APPEND optional_definitions TESERRA_HAS_${upper_dependency}=1)
        else()
            list(APPEND optional_definitions TESERRA_HAS_${upper_dependency}=0)
        endif()
    endforeach()

    # Layer rule: a module may only depend on modules in strictly lower layers.
    foreach(dependency IN LISTS arg_DEPENDS)
        get_property(dependency_known GLOBAL PROPERTY TESERRA_MODULE_LAYER_${dependency} SET)
        get_property(dependency_layer GLOBAL PROPERTY TESERRA_MODULE_LAYER_${dependency})
        if(NOT dependency_known)
            message(FATAL_ERROR
                "teserra_add_module(${name}): unknown module '${dependency}' in DEPENDS. "
                "Modules are added in layer order; a dependency must be defined before its users.")
        endif()
        if(NOT dependency_layer LESS arg_LAYER)
            message(FATAL_ERROR
                "Layer violation: module '${name}' (layer ${arg_LAYER}) must not depend on module "
                "'${dependency}' (layer ${dependency_layer}). A module may only depend on modules in "
                "strictly lower layers; see docs/architecture/overview.md#how-the-layering-is-enforced.")
        endif()
    endforeach()

    set(target teserra_${name})
    _teserra_pascal_case(alias_name "${name}")

    add_library(${target} STATIC)
    add_library(Teserra::${alias_name} ALIAS ${target})
    set_property(GLOBAL PROPERTY TESERRA_MODULE_LAYER_${name} ${arg_LAYER})
    set_property(GLOBAL APPEND PROPERTY TESERRA_MODULES ${name})

    target_compile_features(${target} PUBLIC cxx_std_20)
    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_SCAN_FOR_MODULES OFF
        EXPORT_NAME ${alias_name}
    )
    teserra_set_warnings(${target})

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_sources(${target}
            PUBLIC FILE_SET HEADERS
                BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
                FILES ${arg_PUBLIC_HEADERS}
        )
    elseif(arg_PUBLIC_HEADERS)
        message(FATAL_ERROR "teserra_add_module(${name}): PUBLIC_HEADERS given but include/ does not exist")
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    endif()
    target_sources(${target} PRIVATE ${arg_SOURCES})

    foreach(dependency IN LISTS arg_DEPENDS)
        target_link_libraries(${target} PUBLIC teserra_${dependency})
    endforeach()
    target_compile_definitions(${target} PRIVATE ${optional_definitions})
endfunction()

function(teserra_link_third_party name visibility)
    set(target teserra_${name})
    if(NOT TARGET ${target})
        message(FATAL_ERROR "teserra_link_third_party: '${name}' is not a Teserra module")
    endif()
    if(NOT visibility MATCHES "^(PUBLIC|PRIVATE|INTERFACE)$")
        message(FATAL_ERROR
            "teserra_link_third_party(${name}): visibility must be PUBLIC, PRIVATE or INTERFACE, got '${visibility}'")
    endif()
    if(ARGC LESS 3)
        message(FATAL_ERROR "teserra_link_third_party(${name}): no libraries given")
    endif()

    foreach(library IN LISTS ARGN)
        if(NOT TARGET ${library})
            message(FATAL_ERROR
                "teserra_link_third_party(${name}): '${library}' is not a target. "
                "Call find_package() for it first and link the imported target (e.g. spdlog::spdlog).")
        endif()
    endforeach()
    target_link_libraries(${target} ${visibility} ${ARGN})
endfunction()
