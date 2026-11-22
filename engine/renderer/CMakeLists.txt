teserra_add_module(renderer LAYER 3
    # Only the abstract RHI: backends are chosen by teserra_engine (docs/architecture/overview.md).
    DEPENDS core rhi assets
    PUBLIC_HEADERS
        include/teserra/renderer/camera.hpp
        include/teserra/renderer/camera_data.hpp
        include/teserra/renderer/frame_packet.hpp
        include/teserra/renderer/render_pass.hpp
        include/teserra/renderer/renderer.hpp
        include/teserra/renderer/upload_ring.hpp
    SOURCES
        src/camera.cpp
        src/renderer.cpp
        src/upload_ring.cpp
)

if(TESERRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
