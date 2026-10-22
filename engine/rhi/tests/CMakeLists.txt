teserra_add_test(rhi
    SOURCES
        create_device_test.cpp
        descriptor_test.cpp
        format_test.cpp
    DEPENDS rhi
)

# Every public header compiles on its own and keeps backend headers out (the leak check covers names).
teserra_add_header_check(rhi)
