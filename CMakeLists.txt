cmake_minimum_required(VERSION 3.24)

project(PlaygroundOSSModernization VERSION 1.0.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(cmake/PlaygroundPlatform.cmake)

option(
    PLAYGROUND_BUILD_PLATFORM_PROBE
    "Build the platform-contract diagnostic executable"
    ${PLAYGROUND_PLATFORM_CAN_RUN_HOST_TOOLS}
)

add_subdirectory(Engine/modern/platform)

message(STATUS "Playground target platform: ${PLAYGROUND_TARGET_PLATFORM}")
message(STATUS "Playground target architecture: ${PLAYGROUND_TARGET_ARCHITECTURE}")
message(STATUS "Playground pointer width: ${PLAYGROUND_TARGET_POINTER_BITS}")
message(STATUS "Playground legacy engine graph: intentionally unchanged")
