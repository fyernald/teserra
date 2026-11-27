# Install rules and the CMake package (#143): `cmake --install <build> --prefix <dir>` installs the engine's
# static libraries, their public headers and lib/cmake/Teserra/, so that a game finds it with
#
#   find_package(Teserra 0.2 REQUIRED)
#   target_link_libraries(game PRIVATE Teserra::Engine)
#
# The layout follows GNUInstallDirs. The targets keep the names of the build tree (Teserra::Core, ...,
# Teserra::Engine). The libraries are static, so their private dependencies are link dependencies of the game:
# TeserraConfig.cmake finds the same packages this build found (find_dependency), and the header-only ones
# (stb, miniaudio) are compiled into the libraries. Included by the root CMakeLists.txt when TESERRA_INSTALL is
# on (the default for a top-level build).

include_guard(GLOBAL)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(TESERRA_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/Teserra" CACHE STRING
    "Where the CMake package (TeserraConfig.cmake) is installed, relative to the prefix")
mark_as_advanced(TESERRA_INSTALL_CMAKEDIR)

get_property(_teserra_modules GLOBAL PROPERTY TESERRA_MODULES)
set(_teserra_install_targets "")
foreach(module IN LISTS _teserra_modules)
    list(APPEND _teserra_install_targets teserra_${module})
endforeach()
# The vendored OpenGL loader is a private dependency of the OpenGL backend, so the game links it too.
if(TARGET teserra_glad)
    list(APPEND _teserra_install_targets teserra_glad)
endif()

install(TARGETS ${_teserra_install_targets}
    EXPORT TeserraTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(EXPORT TeserraTargets
    NAMESPACE Teserra::
    DESTINATION "${TESERRA_INSTALL_CMAKEDIR}"
)

# The packages TeserraConfig.cmake finds, as this build found them: only those its modules link.
set(TESERRA_PACKAGE_DEPENDENCIES "")
macro(_teserra_package_dependency)
    string(JOIN " " _line ${ARGN})
    string(APPEND TESERRA_PACKAGE_DEPENDENCIES "find_dependency(${_line})\n")
endmacro()
_teserra_package_dependency(glm CONFIG)
_teserra_package_dependency(tl-expected CONFIG)
_teserra_package_dependency(spdlog CONFIG)
if(TESERRA_ENABLE_PROFILING)
    _teserra_package_dependency(Tracy CONFIG)
endif()
# Imported targets are scoped to the directory that found them, so the modules' link libraries tell.
get_target_property(_teserra_platform_libraries teserra_platform LINK_LIBRARIES)
if("glfw" IN_LIST _teserra_platform_libraries)
    _teserra_package_dependency(glfw3 CONFIG)
endif()
if(TARGET teserra_assets)
    _teserra_package_dependency(yyjson CONFIG)
endif()
if(TARGET teserra_debug_ui)
    _teserra_package_dependency(imgui CONFIG)
endif()
if(TARGET teserra_audio AND UNIX)
    _teserra_package_dependency(Threads)
endif()

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/TeserraConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/TeserraConfig.cmake"
    INSTALL_DESTINATION "${TESERRA_INSTALL_CMAKEDIR}"
)
# Before 1.0, minor versions may break the API (release-process.md#versioning): 0.2 accepts 0.2.x only.
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/TeserraConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMinorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/TeserraConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/TeserraConfigVersion.cmake"
    DESTINATION "${TESERRA_INSTALL_CMAKEDIR}"
)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/teserra"
)
