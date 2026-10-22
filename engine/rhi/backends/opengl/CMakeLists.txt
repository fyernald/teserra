# OpenGL 4.5 backend of the RHI (docs/architecture/rhi.md#opengl-backend-mapping, ADR-0004). Layer 3: only
# the engine and tests link it. GL and glad stay private to this module (ADR-0009).

# The vendored glad loader (third_party/glad/README.md), C code built without the Teserra warning set.
add_library(teserra_glad STATIC third_party/glad/src/gl.c)
target_include_directories(teserra_glad PUBLIC third_party/glad/include)
set_target_properties(teserra_glad PROPERTIES FOLDER "third_party")

teserra_add_module(rhi_opengl LAYER 3
    DEPENDS core platform rhi
    PUBLIC_HEADERS
        include/teserra/rhi_opengl/opengl_backend.hpp
    SOURCES
        src/gl_debug.cpp
        src/gl_device.cpp
        src/opengl_backend.cpp
)
teserra_link_third_party(rhi_opengl PRIVATE teserra_glad)

if(TESERRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
