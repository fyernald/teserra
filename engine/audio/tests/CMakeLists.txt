teserra_add_test(audio
    SOURCES
        audio_clip_test.cpp
        audio_decode_test.cpp
        audio_engine_test.cpp
        audio_device_test.cpp
    DEPENDS audio
)
# The committed fixtures (tools/scripts/make_audio_fixtures.py).
target_compile_definitions(teserra_audio_tests PRIVATE TESERRA_AUDIO_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")

# Every public header compiles on its own and keeps miniaudio out.
teserra_add_header_check(audio)
