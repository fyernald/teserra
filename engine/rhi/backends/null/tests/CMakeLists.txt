teserra_add_test(rhi_null
    SOURCES
        null_binding_test.cpp
        null_device_test.cpp
    DEPENDS rhi_null platform
)

teserra_add_header_check(rhi_null)
