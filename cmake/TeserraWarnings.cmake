# Compiler warnings and conformance flags for Teserra targets.
#
#   teserra_set_warnings(<target>)
#
# Applies the warning set from docs/guides/coding-standards.md#language-level to <target> (PRIVATE, so
# consumers are not affected) and turns warnings into errors when TESERRA_WARNINGS_AS_ERRORS is ON.
# Third-party headers come in through imported targets, whose include directories are SYSTEM, so
# their warnings do not count.

include_guard(GLOBAL)

function(teserra_set_warnings target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "teserra_set_warnings: '${target}' is not a target")
    endif()

    if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        # cl.exe. Conformance flags are part of the language level (ADR-0002), not optional warnings.
        set(warnings /W4)
        set(conformance /permissive- /Zc:__cplusplus /utf-8)
        set(as_errors /WX)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        # clang-cl: MSVC-style driver that also understands Clang's -W flags.
        set(warnings /W4 -Wpedantic -Wconversion -Wsign-conversion -Wshadow)
        set(conformance /permissive- /Zc:__cplusplus /utf-8)
        set(as_errors /WX)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set(warnings -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow)
        set(conformance)
        set(as_errors -Werror)
    else()
        message(WARNING "teserra_set_warnings: no warning flags known for compiler '${CMAKE_CXX_COMPILER_ID}'")
        return()
    endif()

    target_compile_options(${target} PRIVATE ${warnings} ${conformance})
    if(TESERRA_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE ${as_errors})
    endif()
endfunction()
