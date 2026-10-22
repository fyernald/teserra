teserra_add_test(engine
    SOURCES
        app_test.cpp
        app_window_test.cpp
    DEPENDS engine
)
target_include_directories(teserra_engine_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

teserra_add_header_check(engine)
