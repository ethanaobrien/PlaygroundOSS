find_package(ZLIB REQUIRED)

add_library(playground_asset_bootstrap STATIC
    src/AssetBootstrap.cpp
    "${PROJECT_SOURCE_DIR}/Engine/libs/minizip/ioapi.c"
    "${PROJECT_SOURCE_DIR}/Engine/libs/minizip/unzip.c")
add_library(Playground::AssetBootstrap ALIAS playground_asset_bootstrap)
target_compile_features(playground_asset_bootstrap PUBLIC cxx_std_17)
target_compile_definitions(playground_asset_bootstrap PRIVATE IOAPI_NO_64)
target_compile_options(playground_asset_bootstrap PRIVATE
    $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wno-unknown-pragmas>
    $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wno-unused-parameter>
    $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wno-old-style-definition>)
target_include_directories(playground_asset_bootstrap
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE "${PROJECT_SOURCE_DIR}/Engine/libs/minizip")
target_link_libraries(playground_asset_bootstrap PUBLIC ZLIB::ZLIB)

if(BUILD_TESTING AND PLAYGROUND_PLATFORM_CAN_RUN_HOST_TOOLS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(playground-asset-bootstrap-probe
        tools/asset_bootstrap_probe.cpp)
    target_compile_features(playground-asset-bootstrap-probe PRIVATE cxx_std_17)
    target_link_libraries(playground-asset-bootstrap-probe PRIVATE
        Playground::AssetBootstrap)
    add_test(NAME playground-asset-bootstrap
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/scripts/test_asset_bootstrap.py"
            "$<TARGET_FILE:playground-asset-bootstrap-probe>")
    add_test(NAME playground-switch-package-tools
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/scripts/test_switch_package_tools.py"
            "${PROJECT_SOURCE_DIR}/scripts/package_switch_nsp.py")
endif()
