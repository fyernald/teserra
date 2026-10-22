teserra_add_module(rhi LAYER 2
    DEPENDS core platform
    PUBLIC_HEADERS
        include/teserra/rhi/buffer.hpp
        include/teserra/rhi/command_list.hpp
        include/teserra/rhi/commands.hpp
        include/teserra/rhi/device.hpp
        include/teserra/rhi/format.hpp
        include/teserra/rhi/handles.hpp
        include/teserra/rhi/limits.hpp
        include/teserra/rhi/pipeline.hpp
        include/teserra/rhi/render_pass.hpp
        include/teserra/rhi/sampler.hpp
        include/teserra/rhi/shader.hpp
        include/teserra/rhi/swapchain.hpp
        include/teserra/rhi/texture.hpp
    SOURCES
        src/command_list.cpp
        src/device.cpp
        src/format.cpp
)

if(TESERRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Backends are layer-3 modules that implement Device, each behind its option. The null backend follows
# with #29.
if(TESERRA_RHI_OPENGL)
    add_subdirectory(backends/opengl)
endif()
