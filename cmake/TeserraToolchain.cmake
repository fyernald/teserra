# Toolchain file used by CMakePresets.json. Forwards to the vcpkg toolchain in $VCPKG_ROOT and fails
# early with an actionable message when vcpkg cannot be found (docs/guides/build-and-ci.md).

if(NOT DEFINED ENV{VCPKG_ROOT} OR "$ENV{VCPKG_ROOT}" STREQUAL "")
    message(FATAL_ERROR
        "VCPKG_ROOT is not set: point it to your vcpkg directory (see docs/guides/build-and-ci.md#prerequisites-windows).")
endif()

set(_teserra_vcpkg_toolchain "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
if(NOT EXISTS "${_teserra_vcpkg_toolchain}")
    message(FATAL_ERROR
        "VCPKG_ROOT is '$ENV{VCPKG_ROOT}', but ${_teserra_vcpkg_toolchain} does not exist: point VCPKG_ROOT to a vcpkg directory.")
endif()

include("${_teserra_vcpkg_toolchain}")
