teserra_add_test(assets
    SOURCES
        image_test.cpp
    DEPENDS assets
)

# Every public header compiles on its own and keeps stb_image out.
teserra_add_header_check(assets)
