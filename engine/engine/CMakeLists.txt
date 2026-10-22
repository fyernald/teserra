teserra_add_module(engine LAYER 6
    DEPENDS platform core
    PUBLIC_HEADERS
        include/teserra/engine/app.hpp
    SOURCES
        src/app.cpp
)

if(TESERRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
