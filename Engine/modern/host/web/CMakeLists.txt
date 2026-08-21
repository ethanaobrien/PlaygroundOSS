add_library(playground_web_host STATIC
    "${PROJECT_SOURCE_DIR}/Engine/modern/host/sdl/src/DesktopHost.c")
add_library(Playground::WebHost ALIAS playground_web_host)
target_compile_features(playground_web_host PUBLIC c_std_11)
target_include_directories(playground_web_host PUBLIC
    "${PROJECT_SOURCE_DIR}/Engine/modern/host/sdl/include")
target_link_libraries(playground_web_host PUBLIC SDL3::SDL3)

if(NOT TARGET playground_engine_runtime)
    return()
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(playground-web-engine
    src/WebBootstrap.cpp
    src/WebEngineMain.cpp)
target_compile_features(playground-web-engine PRIVATE cxx_std_17)
target_include_directories(playground-web-engine PRIVATE src)
target_compile_definitions(playground-web-engine PRIVATE
    PLAYGROUND_WEB_APP_ASSETS_URL="${PLAYGROUND_WEB_APP_ASSETS_URL}"
    PLAYGROUND_WEB_APP_ASSETS_METADATA_URL="${PLAYGROUND_WEB_APP_ASSETS_METADATA_URL}")
target_link_libraries(playground-web-engine PRIVATE
    Playground::WebHost
    Playground::AssetBootstrap
    $<LINK_LIBRARY:WHOLE_ARCHIVE,playground_engine_runtime>
    Playground::PlatformContract
    MbedTLS::mbedcrypto)

set_target_properties(playground-web-engine PROPERTIES
    OUTPUT_NAME index
    SUFFIX ".html"
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")

target_link_options(playground-web-engine PRIVATE
    "SHELL:-pthread"
    "SHELL:-sWASMFS=1"
    "SHELL:-sFORCE_FILESYSTEM=1"
    "SHELL:-sPROXY_TO_PTHREAD=1"
    "SHELL:-sPTHREAD_POOL_SIZE=12"
    "SHELL:-sALLOW_MEMORY_GROWTH=1"
    "SHELL:-sINITIAL_MEMORY=268435456"
    "SHELL:-sMAXIMUM_MEMORY=2147483648"
    "SHELL:-sSTACK_SIZE=8388608"
    "SHELL:-sOFFSCREENCANVAS_SUPPORT=1"
    "SHELL:-sFULL_ES2=1"
    "SHELL:-sMIN_WEBGL_VERSION=1"
    "SHELL:-sMAX_WEBGL_VERSION=2"
    "SHELL:-sEXIT_RUNTIME=0"
    "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html")

if(PLAYGROUND_APP_ASSETS_ZIP)
    get_filename_component(_web_appassets "${PLAYGROUND_APP_ASSETS_ZIP}"
        ABSOLUTE)
    if(NOT EXISTS "${_web_appassets}")
        message(FATAL_ERROR
            "PLAYGROUND_APP_ASSETS_ZIP does not exist: ${_web_appassets}")
    endif()
    set(_web_metadata "${CMAKE_CURRENT_BINARY_DIR}/AppAssets.metadata")
    add_custom_command(TARGET playground-web-engine POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_web_appassets}" "$<TARGET_FILE_DIR:playground-web-engine>/AppAssets.zip"
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/scripts/generate_appassets_metadata.py"
            "${_web_appassets}" "${_web_metadata}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_web_metadata}" "$<TARGET_FILE_DIR:playground-web-engine>/AppAssets.metadata"
        VERBATIM)
elseif(PLAYGROUND_WEB_APP_ASSETS_METADATA)
    add_custom_command(TARGET playground-web-engine POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${PLAYGROUND_WEB_APP_ASSETS_METADATA}"
            "$<TARGET_FILE_DIR:playground-web-engine>/AppAssets.metadata"
        VERBATIM)
endif()

add_custom_target(playground-web-serve
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/serve_web.py"
        "$<TARGET_FILE_DIR:playground-web-engine>"
        --port 8765
    DEPENDS playground-web-engine
    USES_TERMINAL
    VERBATIM)
