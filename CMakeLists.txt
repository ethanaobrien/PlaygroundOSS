cmake_minimum_required(VERSION 3.24)

project(PlaygroundOSSModernization VERSION 1.0.0 LANGUAGES C CXX)

set(PLAYGROUND_APP_ASSETS_ZIP "" CACHE FILEPATH
    "Path to the original AppAssets.zip embedded unchanged in packaged builds")
set(PLAYGROUND_SWITCH_ICON_JPEG "" CACHE FILEPATH
    "Path to a 256x256 JPEG used by installed Switch package staging")
set(PLAYGROUND_SWITCH_TITLE_ID "0x0100F59153491000" CACHE STRING
    "Nintendo Switch application title ID")
set(PLAYGROUND_SWITCH_SAVE_SIZE "0x10000000" CACHE STRING
    "Initial and maximum per-user SaveData size")
set(PLAYGROUND_SWITCH_SAVE_JOURNAL_SIZE "0x04000000" CACHE STRING
    "Per-user SaveData journal size")
set(PLAYGROUND_SWITCH_CACHE_SIZE "0x200000000" CACHE STRING
    "Initial CacheStorage data size")
set(PLAYGROUND_SWITCH_CACHE_JOURNAL_SIZE "0x04000000" CACHE STRING
    "CacheStorage journal size")
set(PLAYGROUND_SWITCH_CACHE_MAX_SIZE "0x400000000" CACHE STRING
    "Maximum combined CacheStorage data and journal size")

include(GNUInstallDirs)
include(CTest)
include(cmake/PlaygroundPlatform.cmake)

set(_playground_desktop_platforms windows linux macos)

option(
    PLAYGROUND_BUILD_PLATFORM_PROBE
    "Build the platform-contract diagnostic executable"
    ${PLAYGROUND_PLATFORM_CAN_RUN_HOST_TOOLS}
)

set(_playground_desktop_host_default OFF)
if(PLAYGROUND_PLATFORM_CAN_RUN_HOST_TOOLS
   AND PLAYGROUND_TARGET_PLATFORM IN_LIST _playground_desktop_platforms)
    set(_playground_desktop_host_default ON)
endif()
option(
    PLAYGROUND_BUILD_DESKTOP_HOST
    "Build the SDL3 desktop host and diagnostic executable"
    ${_playground_desktop_host_default}
)

set(_playground_switch_host_default OFF)
if(PLAYGROUND_TARGET_PLATFORM STREQUAL switch)
    set(_playground_switch_host_default ON)
endif()
option(
    PLAYGROUND_BUILD_SWITCH_HOST
    "Build the Nintendo Switch/libnx host"
    ${_playground_switch_host_default}
)

set(_playground_engine_runtime_default OFF)
if(PLAYGROUND_TARGET_PLATFORM STREQUAL linux
   OR PLAYGROUND_TARGET_PLATFORM STREQUAL windows)
    set(_playground_engine_runtime_default ON)
endif()
option(
    PLAYGROUND_BUILD_ENGINE_RUNTIME
    "Compile the reconstructed engine graph for the selected platform"
    ${_playground_engine_runtime_default}
)

add_subdirectory(Engine/modern/platform)

# The generic archive bootstrap is consumed by the Switch host and exercised
# by native host tests.  Android keeps its established Java/package bootstrap
# and does not link this library, so do not compile a second copy of legacy
# minizip into cross-ABI contract-only builds.
if(PLAYGROUND_TARGET_PLATFORM STREQUAL switch
   OR PLAYGROUND_PLATFORM_CAN_RUN_HOST_TOOLS)
    add_subdirectory(Engine/modern/bootstrap)
endif()

if(PLAYGROUND_TARGET_PLATFORM STREQUAL switch)
    add_subdirectory(Engine/modern/runtime/switch)
endif()

if(PLAYGROUND_BUILD_DESKTOP_HOST
   OR (PLAYGROUND_BUILD_ENGINE_RUNTIME
       AND PLAYGROUND_TARGET_PLATFORM IN_LIST _playground_desktop_platforms))
    include(cmake/PlaygroundSDL.cmake)
endif()

if(PLAYGROUND_BUILD_ENGINE_RUNTIME)
    add_subdirectory(Engine/modern/runtime)
endif()

if(PLAYGROUND_BUILD_DESKTOP_HOST)
    add_subdirectory(Engine/modern/host/sdl)
endif()

if(PLAYGROUND_BUILD_SWITCH_HOST)
    if(NOT PLAYGROUND_TARGET_PLATFORM STREQUAL switch)
        message(FATAL_ERROR
            "PLAYGROUND_BUILD_SWITCH_HOST requires the libnx Switch toolchain")
    endif()
    add_subdirectory(Engine/modern/host/switch)
endif()

message(STATUS "Playground target platform: ${PLAYGROUND_TARGET_PLATFORM}")
message(STATUS "Playground target architecture: ${PLAYGROUND_TARGET_ARCHITECTURE}")
message(STATUS "Playground pointer width: ${PLAYGROUND_TARGET_POINTER_BITS}")
message(STATUS "Playground legacy engine graph: intentionally unchanged")
