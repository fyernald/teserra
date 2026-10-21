teserra_add_module(core LAYER 0
    PUBLIC_HEADERS
        include/teserra/core/config.hpp
        include/teserra/core/types.hpp
    SOURCES
        src/version.cpp
)

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
