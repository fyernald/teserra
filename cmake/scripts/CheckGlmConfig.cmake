# Fails if glm is configured anywhere but in engine/core/CMakeLists.txt (ADR-0008): a GLM_* macro defined
# in a source file, or set in any other CMake file, would give translation units different glm settings
# and violate the ODR. Run by the CTest test cmake.glm_configured_only_by_core.
#
#   cmake -DTESERRA_SOURCE_DIR=<repository root> -P CheckGlmConfig.cmake

# GLOB RELATIVE needs an absolute base directory.
file(REAL_PATH "${TESERRA_SOURCE_DIR}" TESERRA_SOURCE_DIR)
set(allowed "engine/core/CMakeLists.txt")
set(violations "")

# Source directories only: build trees contain third-party sources that legitimately mention GLM_*.
# The top-level CMakeLists.txt is added separately, because a recursive glob would search every
# directory below the root for that name.
file(GLOB_RECURSE sources RELATIVE "${TESERRA_SOURCE_DIR}"
    "${TESERRA_SOURCE_DIR}/engine/*" "${TESERRA_SOURCE_DIR}/examples/*" "${TESERRA_SOURCE_DIR}/tests/*"
    "${TESERRA_SOURCE_DIR}/tools/*" "${TESERRA_SOURCE_DIR}/benchmarks/*" "${TESERRA_SOURCE_DIR}/editor/*"
    "${TESERRA_SOURCE_DIR}/cmake/*"
)
list(APPEND sources "CMakeLists.txt")
foreach(file IN LISTS sources)
    if(file STREQUAL allowed OR file MATCHES "(^|/)third_party/")
        continue()
    endif()
    if(file MATCHES "\.(cpp|hpp|h|inl)$")
        set(pattern "#[ \t]*define[ \t]+GLM_")
    elseif(file MATCHES "(CMakeLists\.txt|\.cmake)$" AND NOT file STREQUAL "cmake/scripts/CheckGlmConfig.cmake")
        set(pattern "GLM_[A-Z_]+")
    else()
        continue()
    endif()
    file(STRINGS "${TESERRA_SOURCE_DIR}/${file}" matches REGEX "${pattern}")
    if(matches)
        list(APPEND violations "${file}")
    endif()
endforeach()

if(violations)
    list(JOIN violations "\n  " listed)
    message(FATAL_ERROR
        "glm may only be configured in ${allowed} (ADR-0008); GLM_* macros found in:\n  ${listed}")
endif()
message(STATUS "CheckGlmConfig: glm is configured only by teserra_core")
