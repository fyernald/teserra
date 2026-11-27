teserra_add_test(audio
    SOURCES
        audio_clip_test.cpp
        audio_engine_test.cpp
        audio_device_test.cpp
    DEPENDS audio
)

# Every public header compiles on its own and keeps miniaudio out.
teserra_add_header_check(audio)
