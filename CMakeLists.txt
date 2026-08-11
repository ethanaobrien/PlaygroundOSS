cmake_minimum_required(VERSION 3.24)

project(PlaygroundOSSModernization VERSION 1.0.0 LANGUAGES C CXX)

include(GNUInstallDirs)
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

set(_playground_engine_runtime_default OFF)
if(PLAYGROUND_TARGET_PLATFORM STREQUAL linux)
    set(_playground_engine_runtime_default ON)
endif()
option(
    PLAYGROUND_BUILD_ENGINE_RUNTIME
    "Compile the reconstructed engine graph for the selected platform"
    ${_playground_engine_runtime_default}
)

add_subdirectory(Engine/modern/platform)

if(PLAYGROUND_BUILD_ENGINE_RUNTIME OR PLAYGROUND_BUILD_DESKTOP_HOST)
    include(cmake/PlaygroundSDL.cmake)
endif()

if(PLAYGROUND_BUILD_ENGINE_RUNTIME)
    add_subdirectory(Engine/modern/runtime)
endif()

if(PLAYGROUND_BUILD_DESKTOP_HOST)
    add_subdirectory(Engine/modern/host/sdl)
endif()

message(STATUS "Playground target platform: ${PLAYGROUND_TARGET_PLATFORM}")
message(STATUS "Playground target architecture: ${PLAYGROUND_TARGET_ARCHITECTURE}")
message(STATUS "Playground pointer width: ${PLAYGROUND_TARGET_POINTER_BITS}")
message(STATUS "Playground legacy engine graph: intentionally unchanged")
