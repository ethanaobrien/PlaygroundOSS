set(_playground_supported_platforms android windows linux macos switch emscripten)

if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(_playground_detected_platform emscripten)
elseif(ANDROID)
    set(_playground_detected_platform android)
elseif(NINTENDO_SWITCH OR CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    set(_playground_detected_platform switch)
elseif(WIN32)
    set(_playground_detected_platform windows)
elseif(APPLE)
    set(_playground_detected_platform macos)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_playground_detected_platform linux)
else()
    message(FATAL_ERROR "Unsupported target system: ${CMAKE_SYSTEM_NAME}")
endif()

set(
    PLAYGROUND_TARGET_PLATFORM
    "${_playground_detected_platform}"
    CACHE STRING
    "Playground target platform"
)
set_property(
    CACHE PLAYGROUND_TARGET_PLATFORM
    PROPERTY STRINGS ${_playground_supported_platforms}
)

if(NOT PLAYGROUND_TARGET_PLATFORM IN_LIST _playground_supported_platforms)
    message(FATAL_ERROR
        "PLAYGROUND_TARGET_PLATFORM must be one of: ${_playground_supported_platforms}"
    )
endif()

if(NOT PLAYGROUND_TARGET_PLATFORM STREQUAL _playground_detected_platform)
    message(FATAL_ERROR
        "Selected platform ${PLAYGROUND_TARGET_PLATFORM} does not match the "
        "active ${CMAKE_SYSTEM_NAME} toolchain (${_playground_detected_platform})"
    )
endif()

if(NOT CMAKE_SIZEOF_VOID_P)
    message(FATAL_ERROR "The compiler did not report a pointer size")
endif()

math(EXPR PLAYGROUND_TARGET_POINTER_BITS "${CMAKE_SIZEOF_VOID_P} * 8")
if(PLAYGROUND_TARGET_PLATFORM STREQUAL emscripten)
    set(PLAYGROUND_TARGET_ARCHITECTURE wasm32)
else()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" PLAYGROUND_TARGET_ARCHITECTURE)
endif()
if(PLAYGROUND_TARGET_ARCHITECTURE STREQUAL "")
    set(PLAYGROUND_TARGET_ARCHITECTURE unknown)
endif()

foreach(_platform IN LISTS _playground_supported_platforms)
    string(TOUPPER "${_platform}" _platform_upper)
    if(PLAYGROUND_TARGET_PLATFORM STREQUAL _platform)
        set("PLAYGROUND_PLATFORM_${_platform_upper}" 1)
    else()
        set("PLAYGROUND_PLATFORM_${_platform_upper}" 0)
    endif()
endforeach()

if(CMAKE_CROSSCOMPILING)
    set(PLAYGROUND_PLATFORM_CAN_RUN_HOST_TOOLS OFF)
else()
    set(PLAYGROUND_PLATFORM_CAN_RUN_HOST_TOOLS ON)
endif()
