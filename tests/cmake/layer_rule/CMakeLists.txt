# Fixture project for the layer rule in cmake/TeserraModule.cmake. TESERRA_FIXTURE_CASE selects the
# module graph; tests/cmake/CMakeLists.txt configures every case and checks the result.

cmake_minimum_required(VERSION 3.28)
project(TeserraLayerRuleFixture LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/TeserraModule.cmake")

set(source "${CMAKE_CURRENT_LIST_DIR}/empty.cpp")

if(TESERRA_FIXTURE_CASE STREQUAL "accepts_lower_layers")
    teserra_add_module(core LAYER 0 SOURCES ${source})
    teserra_add_module(platform LAYER 1 DEPENDS core SOURCES ${source})
    teserra_add_module(rhi LAYER 2 DEPENDS platform SOURCES ${source})
    teserra_add_module(rhi_null LAYER 3 DEPENDS rhi core SOURCES ${source})

    foreach(pair IN ITEMS "Core:teserra_core" "Platform:teserra_platform" "RhiNull:teserra_rhi_null")
        string(REPLACE ":" ";" pair "${pair}")
        list(GET pair 0 alias)
        list(GET pair 1 expected)
        if(NOT TARGET Teserra::${alias})
            message(FATAL_ERROR "Fixture: alias Teserra::${alias} was not created")
        endif()
        get_target_property(aliased Teserra::${alias} ALIASED_TARGET)
        if(NOT aliased STREQUAL expected)
            message(FATAL_ERROR "Fixture: Teserra::${alias} aliases '${aliased}', expected '${expected}'")
        endif()
    endforeach()
elseif(TESERRA_FIXTURE_CASE STREQUAL "rejects_higher_layer")
    teserra_add_module(platform LAYER 1 SOURCES ${source})
    teserra_add_module(core LAYER 0 DEPENDS platform SOURCES ${source})
elseif(TESERRA_FIXTURE_CASE STREQUAL "rejects_same_layer")
    teserra_add_module(rhi LAYER 2 SOURCES ${source})
    teserra_add_module(assets LAYER 2 DEPENDS rhi SOURCES ${source})
elseif(TESERRA_FIXTURE_CASE STREQUAL "rejects_unknown_module")
    teserra_add_module(core LAYER 0 DEPENDS platform SOURCES ${source})
else()
    message(FATAL_ERROR "Fixture: unknown TESERRA_FIXTURE_CASE '${TESERRA_FIXTURE_CASE}'")
endif()
