teserra_add_test(assets
    SOURCES
        asset_fallback_test.cpp
        asset_id_test.cpp
        asset_loader_test.cpp
        asset_registry_test.cpp
        image_test.cpp
        json_test.cpp
        mount_table_test.cpp
        tiled_test.cpp
    DEPENDS assets
)

# Every public header compiles on its own and keeps stb_image and yyjson out.
teserra_add_header_check(assets)
