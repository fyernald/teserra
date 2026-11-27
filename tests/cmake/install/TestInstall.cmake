# cmake.install.find_package (#143): installs this build into a scratch prefix, then configures, builds and runs
# tests/cmake/install_consumer against it with find_package(Teserra <major>.<minor>), and checks that another
# minor version is refused (SameMinorVersion before 1.0).
#
#   cmake -DTESERRA_SOURCE_DIR=... -DTESERRA_BUILD_DIR=... -DTESERRA_WORK_DIR=... -DTESERRA_VERSION=0.2
#         -DTESERRA_OTHER_VERSION=0.3 -DTESERRA_GENERATOR=... -DTESERRA_MAKE_PROGRAM=... -DTESERRA_CXX_COMPILER=...
#         -DTESERRA_PREFIX_PATH=... -DTESERRA_BUILD_TYPE=... -P TestInstall.cmake

foreach(variable IN ITEMS TESERRA_SOURCE_DIR TESERRA_BUILD_DIR TESERRA_WORK_DIR TESERRA_VERSION TESERRA_OTHER_VERSION
                          TESERRA_GENERATOR TESERRA_CXX_COMPILER TESERRA_BUILD_TYPE)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "TestInstall.cmake: ${variable} is not set")
    endif()
endforeach()

set(prefix "${TESERRA_WORK_DIR}/prefix")
file(REMOVE_RECURSE "${TESERRA_WORK_DIR}")

function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Failed (${result}): ${ARGN}\n${output}")
    endif()
    set(output "${output}" PARENT_SCOPE)
endfunction()

run("${CMAKE_COMMAND}" --install "${TESERRA_BUILD_DIR}" --prefix "${prefix}")
foreach(file IN ITEMS lib/cmake/Teserra/TeserraConfig.cmake lib/cmake/Teserra/TeserraConfigVersion.cmake
                      lib/cmake/Teserra/TeserraTargets.cmake include/teserra/teserra.hpp
                      include/teserra/core/version.hpp share/doc/teserra/THIRD_PARTY_NOTICES.md)
    if(NOT EXISTS "${prefix}/${file}")
        message(FATAL_ERROR "The install has no ${file}")
    endif()
endforeach()
# Nothing private is installed: no glad or miniaudio headers, no sources.
file(GLOB_RECURSE leaked RELATIVE "${prefix}" "${prefix}/include/glad/*" "${prefix}/include/miniaudio.h"
     "${prefix}/include/*.cpp")
if(leaked)
    message(FATAL_ERROR "The install has private files: ${leaked}")
endif()

set(prefix_path "${prefix}")
if(TESERRA_PREFIX_PATH)
    list(APPEND prefix_path ${TESERRA_PREFIX_PATH})
endif()
# The prefix path is a list: an initial cache file passes it whole.
set(initial_cache "${TESERRA_WORK_DIR}/initial_cache.cmake")
file(WRITE "${initial_cache}" "set(CMAKE_PREFIX_PATH [==[${prefix_path}]==] CACHE STRING \"\")\n")
set(configure_args
    -G "${TESERRA_GENERATOR}"
    -C "${initial_cache}"
    "-DCMAKE_CXX_COMPILER=${TESERRA_CXX_COMPILER}"
    "-DCMAKE_BUILD_TYPE=${TESERRA_BUILD_TYPE}"
)
if(TESERRA_MAKE_PROGRAM)
    list(APPEND configure_args "-DCMAKE_MAKE_PROGRAM=${TESERRA_MAKE_PROGRAM}")
endif()

# Another minor version is refused.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${TESERRA_SOURCE_DIR}/tests/cmake/install_consumer"
        -B "${TESERRA_WORK_DIR}/refused" ${configure_args} "-DTESERRA_EXPECTED_VERSION=${TESERRA_OTHER_VERSION}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(result EQUAL 0 OR NOT output MATCHES "not suitable|Could not find a configuration file|compatible")
    message(FATAL_ERROR "find_package(Teserra ${TESERRA_OTHER_VERSION}) should fail:\n${output}")
endif()

# This version configures, builds and runs.
set(build "${TESERRA_WORK_DIR}/build")
run("${CMAKE_COMMAND}" -S "${TESERRA_SOURCE_DIR}/tests/cmake/install_consumer" -B "${build}" ${configure_args}
    "-DTESERRA_EXPECTED_VERSION=${TESERRA_VERSION}")
run("${CMAKE_COMMAND}" --build "${build}")
find_program(consumer NAMES install_consumer PATHS "${build}" NO_DEFAULT_PATH REQUIRED)
run("${consumer}")
if(NOT output MATCHES "from the installed package: 1 entity, mixed 0.50")
    message(FATAL_ERROR "Unexpected output of the consumer:\n${output}")
endif()
message(STATUS "${output}")
