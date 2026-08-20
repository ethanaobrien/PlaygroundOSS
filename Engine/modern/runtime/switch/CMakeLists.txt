find_package(PkgConfig REQUIRED)
pkg_check_modules(PLAYGROUND_SWITCH_MBEDCRYPTO REQUIRED IMPORTED_TARGET
    mbedcrypto)

add_library(playground_switch_runtime STATIC
    src/SwitchAlbum.cpp
    src/SwitchStorage.cpp
    src/SwitchHash.cpp
    src/SwitchNetwork.cpp
    src/SwitchSqlite.cpp)
add_library(Playground::SwitchRuntime ALIAS playground_switch_runtime)
target_compile_features(playground_switch_runtime PUBLIC cxx_std_17)
target_include_directories(playground_switch_runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${PROJECT_SOURCE_DIR}/Engine/libs/SQLite)
target_compile_definitions(playground_switch_runtime PRIVATE
    PLAYGROUND_SWITCH_SAVE_SIZE=${PLAYGROUND_SWITCH_SAVE_SIZE}
    PLAYGROUND_SWITCH_SAVE_JOURNAL_SIZE=${PLAYGROUND_SWITCH_SAVE_JOURNAL_SIZE}
    PLAYGROUND_SWITCH_CACHE_SIZE=${PLAYGROUND_SWITCH_CACHE_SIZE}
    PLAYGROUND_SWITCH_CACHE_JOURNAL_SIZE=${PLAYGROUND_SWITCH_CACHE_JOURNAL_SIZE})
target_link_libraries(playground_switch_runtime PUBLIC
    Playground::AssetBootstrap
    PkgConfig::PLAYGROUND_SWITCH_MBEDCRYPTO)
