# Spike #137 (never merged): bitmap fonts with stb_truetype against MSDF with msdfgen.
find_package(msdfgen CONFIG REQUIRED)

add_executable(teserra_spike_text main.cpp)
target_link_libraries(teserra_spike_text PRIVATE Teserra::Engine msdfgen::msdfgen-core)
teserra_set_warnings(teserra_spike_text)

teserra_add_shaders(teserra_spike_text EMBED
    NAMESPACE teserra::examples::shaders
    HEADER spike_text/shaders.hpp
    SOURCES
        shaders/text.vert
        shaders/bitmap.frag
        shaders/msdf.frag
)
