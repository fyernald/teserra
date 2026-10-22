teserra_add_module(core LAYER 0
    PUBLIC_HEADERS
        include/teserra/core/assert.hpp
        include/teserra/core/config.hpp
        include/teserra/core/log.hpp
        include/teserra/core/math.hpp
        include/teserra/core/profile.hpp
        include/teserra/core/result.hpp
        include/teserra/core/types.hpp
    SOURCES
        src/assert.cpp
        src/log.cpp
        src/version.cpp
)

# spdlog stays behind the logging facade (ADR-0011): PRIVATE, so neither its headers nor fmt's reach
# users of teserra_core. The header check in tests/ verifies it.
find_package(spdlog CONFIG REQUIRED)
teserra_link_third_party(core PRIVATE spdlog::spdlog)

# glm is PUBLIC: engine headers use its types through <teserra/core/math.hpp> (ADR-0008). Its
# configuration is fixed here, once, as PUBLIC definitions; nothing else may define GLM_* macros, because
# translation units compiled with different glm settings would violate the ODR.
find_package(glm CONFIG REQUIRED)
teserra_link_third_party(core PUBLIC glm::glm-header-only)
target_compile_definitions(teserra_core PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS)

# tl-expected is PUBLIC: Result<T> is tl::expected in engine signatures (ADR-0016). Header-only.
find_package(tl-expected CONFIG REQUIRED)
teserra_link_third_party(core PUBLIC tl::expected)

# TESERRA_ENABLE_ASSERTS is PUBLIC: TESERRA_ASSERT expands in the code of every module and game that uses
# it, so they all check (or skip) assertions the same way (ADR-0016).
if(TESERRA_ENABLE_ASSERTS STREQUAL "DEFAULT")
    set(asserts_enabled "$<IF:$<CONFIG:Debug,RelWithDebInfo>,1,0>")
elseif(TESERRA_ENABLE_ASSERTS)
    set(asserts_enabled 1)
else()
    set(asserts_enabled 0)
endif()
target_compile_definitions(teserra_core PUBLIC "TESERRA_ENABLE_ASSERTS=${asserts_enabled}")

# <teserra/core/version.hpp> is generated from the project version so it has a single source of truth.
set(generated_include_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(version.hpp.in "${generated_include_dir}/teserra/core/version.hpp" @ONLY)
target_sources(teserra_core
    PUBLIC FILE_SET HEADERS
        BASE_DIRS "${generated_include_dir}"
        FILES "${generated_include_dir}/teserra/core/version.hpp"
)

if(TESERRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
