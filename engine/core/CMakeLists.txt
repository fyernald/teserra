teserra_add_module(core LAYER 0
    PUBLIC_HEADERS
        include/teserra/core/config.hpp
        include/teserra/core/log.hpp
        include/teserra/core/profile.hpp
        include/teserra/core/types.hpp
    SOURCES
        src/log.cpp
        src/version.cpp
)

# spdlog stays behind the logging facade (ADR-0011): PRIVATE, so neither its headers nor fmt's reach
# users of teserra_core. The header check in tests/ verifies it.
find_package(spdlog CONFIG REQUIRED)
teserra_link_third_party(core PRIVATE spdlog::spdlog)

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
