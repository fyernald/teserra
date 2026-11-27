teserra_add_module(audio LAYER 3
    # Sound output and mixing (ADR-0027). assets for the clip asset type and its loader; no platform code:
    # miniaudio opens the sound device itself.
    DEPENDS core assets
    PUBLIC_HEADERS
        include/teserra/audio/audio_clip.hpp
        include/teserra/audio/audio_engine.hpp
    SOURCES
        src/audio_clip.cpp
        src/audio_engine.cpp
        src/miniaudio.cpp
)

# miniaudio mixes and plays sound (ADR-0027). It is PRIVATE: src/miniaudio.cpp compiles its implementation,
# and only files under src/ include it. Like stb, it is a header with no CMake package, so it is found
# directly; the include directories of imported targets are SYSTEM, so its own warnings do not count. It
# needs no system libraries at build time: on Linux it opens PulseAudio, PipeWire or ALSA at run time, and
# only needs libdl, pthreads and libm.
find_path(TESERRA_MINIAUDIO_INCLUDE_DIR NAMES miniaudio.h PATH_SUFFIXES miniaudio REQUIRED)
mark_as_advanced(TESERRA_MINIAUDIO_INCLUDE_DIR)
add_library(teserra_miniaudio INTERFACE IMPORTED)
# stb_vorbis decodes OGG Vorbis for miniaudio's decoder; it comes with the stb port that images use.
find_path(TESERRA_STB_VORBIS_INCLUDE_DIR NAMES stb_vorbis.c PATH_SUFFIXES stb REQUIRED)
mark_as_advanced(TESERRA_STB_VORBIS_INCLUDE_DIR)
set_target_properties(teserra_miniaudio PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${TESERRA_MINIAUDIO_INCLUDE_DIR};${TESERRA_STB_VORBIS_INCLUDE_DIR}")
# Build interface only: miniaudio is compiled into teserra_audio, so the installed package does not need it. Its
# system libraries are linked on their own, so that they reach games that link the installed library.
teserra_link_third_party(audio PRIVATE $<BUILD_INTERFACE:teserra_miniaudio>)
if(UNIX)
    find_package(Threads REQUIRED)
    teserra_link_third_party(audio PRIVATE Threads::Threads)
    target_link_libraries(teserra_audio PRIVATE ${CMAKE_DL_LIBS} m)
endif()

if(TESERRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
