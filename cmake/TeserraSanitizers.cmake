# Sanitizers for every target defined after this file is included (#113, docs/guides/build-and-ci.md).
#
# TESERRA_ENABLE_SANITIZERS=ON builds with AddressSanitizer: MSVC's /fsanitize=address, or ASan with
# UndefinedBehaviorSanitizer on Clang and GCC. The vcpkg libraries are not instrumented; with MSVC, the STL's
# container annotations are turned off so instrumented and plain code can link (each other's std::vector
# layouts must agree), which leaves out container-overflow checks but keeps heap, stack and use-after-free
# checks. Run the tests with the sanitizer runtime on PATH (a Visual Studio developer environment has it).

include_guard(GLOBAL)

if(NOT TESERRA_ENABLE_SANITIZERS)
    return()
endif()

if(MSVC)
    add_compile_options(/fsanitize=address)
    # Plain and instrumented code must agree on std::vector, std::string and std::optional (LNK2038 otherwise).
    add_compile_definitions(_DISABLE_VECTOR_ANNOTATION _DISABLE_STRING_ANNOTATION _DISABLE_OPTIONAL_ANNOTATION)
    # /RTC run-time checks and incremental linking do not work with ASan.
    foreach(flags IN ITEMS CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG)
        string(REGEX REPLACE "/RTC[1csu]*" "" ${flags} "${${flags}}")
    endforeach()
    foreach(flags IN ITEMS CMAKE_EXE_LINKER_FLAGS_DEBUG CMAKE_SHARED_LINKER_FLAGS_DEBUG)
        string(REPLACE "/INCREMENTAL" "/INCREMENTAL:NO" ${flags} "${${flags}}")
    endforeach()
    add_link_options(/INCREMENTAL:NO)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
else()
    message(FATAL_ERROR "TESERRA_ENABLE_SANITIZERS: no sanitizer flags known for '${CMAKE_CXX_COMPILER_ID}'")
endif()
message(STATUS "Teserra: building with sanitizers")
