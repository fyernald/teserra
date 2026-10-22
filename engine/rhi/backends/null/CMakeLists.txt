# Null backend of the RHI (docs/architecture/rhi.md#null-backend): a Device without a GPU that validates and
# keeps what it is given, for tests and headless runs. Layer 3: only the engine and tests link it.

teserra_add_module(rhi_null LAYER 3
    DEPENDS core platform rhi
    PUBLIC_HEADERS
        include/teserra/rhi_null/null_device.hpp
    SOURCES
        src/null_device.cpp
)

if(TESERRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
