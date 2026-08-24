add_library(playground_wiiu_host STATIC
    src/WiiUHost.c
    src/WiiUKeyboard.cpp)
add_library(Playground::WiiUHost ALIAS playground_wiiu_host)
target_compile_features(playground_wiiu_host PUBLIC c_std_11)
target_compile_features(playground_wiiu_host PRIVATE cxx_std_17)
target_include_directories(playground_wiiu_host PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(playground_wiiu_host PUBLIC Playground::WiiUANGLE)

add_executable(playground-wiiu-host
    tools/wiiu_host_main.c
    "${PROJECT_SOURCE_DIR}/Engine/modern/runtime/wiiu/src/WiiUCompilerWorkarounds.c")
target_compile_features(playground-wiiu-host PRIVATE c_std_11)
set_target_properties(playground-wiiu-host PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(playground-wiiu-host PRIVATE
    Playground::WiiUHost
    Playground::PlatformContract)

wut_create_rpx(playground-wiiu-host)

set(PLAYGROUND_WIIU_GLSL_COMPILER_RPL "" CACHE FILEPATH
    "Existing CafeGLSL v0.2.0 glslcompiler.rpl (downloaded when empty)")
set(PLAYGROUND_WIIU_CA_BUNDLE "" CACHE FILEPATH
    "Existing Mozilla CA bundle in PEM format (downloaded when empty)")
if(PLAYGROUND_WIIU_GLSL_COMPILER_RPL)
    get_filename_component(_wiiu_glsl_compiler
        "${PLAYGROUND_WIIU_GLSL_COMPILER_RPL}" ABSOLUTE)
    if(NOT EXISTS "${_wiiu_glsl_compiler}")
        message(FATAL_ERROR
            "PLAYGROUND_WIIU_GLSL_COMPILER_RPL does not exist: "
            "${_wiiu_glsl_compiler}")
    endif()
else()
    set(_wiiu_glsl_compiler
        "${CMAKE_CURRENT_BINARY_DIR}/downloads/glslcompiler.rpl")
    if(NOT EXISTS "${_wiiu_glsl_compiler}")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/downloads")
        file(DOWNLOAD
            "https://github.com/Exzap/CafeGLSL/releases/download/v0.2.0/glslcompiler.rpl"
            "${_wiiu_glsl_compiler}"
            EXPECTED_HASH
                "SHA256=536d8ded63d7ad62ef966ee4b26561cd7da528bba94d2ad126325b56b633c0d7"
            TLS_VERIFY ON
            STATUS _wiiu_glsl_download_status)
        list(GET _wiiu_glsl_download_status 0 _wiiu_glsl_download_code)
        if(NOT _wiiu_glsl_download_code EQUAL 0)
            list(GET _wiiu_glsl_download_status 1 _wiiu_glsl_download_error)
            message(FATAL_ERROR
                "Unable to download pinned CafeGLSL runtime: "
                "${_wiiu_glsl_download_error}")
        endif()
    endif()
endif()

if(PLAYGROUND_WIIU_CA_BUNDLE)
    get_filename_component(_wiiu_ca_bundle
        "${PLAYGROUND_WIIU_CA_BUNDLE}" ABSOLUTE)
    if(NOT EXISTS "${_wiiu_ca_bundle}")
        message(FATAL_ERROR
            "PLAYGROUND_WIIU_CA_BUNDLE does not exist: ${_wiiu_ca_bundle}")
    endif()
else()
    set(_wiiu_ca_bundle "${CMAKE_CURRENT_BINARY_DIR}/downloads/cacert.pem")
    if(NOT EXISTS "${_wiiu_ca_bundle}")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/downloads")
        file(DOWNLOAD
            "https://curl.se/ca/cacert-2026-08-13.pem"
            "${_wiiu_ca_bundle}"
            EXPECTED_HASH
                "SHA256=f66dff1bdf8f96060b8177976f8b7d9254bc89bc4db933d769f7384d28480bc9"
            TLS_VERIFY ON
            STATUS _wiiu_ca_download_status)
        list(GET _wiiu_ca_download_status 0 _wiiu_ca_download_code)
        if(NOT _wiiu_ca_download_code EQUAL 0)
            list(GET _wiiu_ca_download_status 1 _wiiu_ca_download_error)
            message(FATAL_ERROR
                "Unable to download pinned Mozilla CA bundle: "
                "${_wiiu_ca_download_error}")
        endif()
    endif()
endif()

set(_wiiu_probe_content "${CMAKE_CURRENT_BINARY_DIR}/probe-content")
set(_wiiu_probe_compiler "${_wiiu_probe_content}/glslcompiler.rpl")
add_custom_command(OUTPUT "${_wiiu_probe_compiler}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_wiiu_probe_content}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${_wiiu_glsl_compiler}" "${_wiiu_probe_compiler}"
    DEPENDS "${_wiiu_glsl_compiler}"
    VERBATIM)
add_custom_target(playground-wiiu-probe-content
    DEPENDS "${_wiiu_probe_compiler}")
set_target_properties(playground-wiiu-probe-content PROPERTIES
    DKP_ASSET_FOLDER "${_wiiu_probe_content}"
    DKP_ASSET_FILES "${_wiiu_probe_compiler}")
wut_create_wuhb(playground-wiiu-host
    CONTENT playground-wiiu-probe-content
    NAME "PlaygroundOSS host probe"
    SHORTNAME "Playground host"
    AUTHOR "PlaygroundOSS contributors")

if(PLAYGROUND_BUILD_ENGINE_RUNTIME)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(playground-wiiu-engine tools/wiiu_engine_main.cpp)
    target_compile_features(playground-wiiu-engine PRIVATE cxx_std_17)
    target_link_libraries(playground-wiiu-engine PRIVATE
        Playground::PlatformContract
        Playground::AssetBootstrap
        "-Wl,--whole-archive"
        playground_engine_runtime
        "-Wl,--no-whole-archive"
        Playground::WiiUHost
        Playground::WiiUANGLE
        "-Wl,--whole-archive"
        z
        "-Wl,--no-whole-archive")
    wut_create_rpx(playground-wiiu-engine)

    if(PLAYGROUND_APP_ASSETS_ZIP)
        get_filename_component(_wiiu_appassets_zip
            "${PLAYGROUND_APP_ASSETS_ZIP}" ABSOLUTE)
        if(NOT EXISTS "${_wiiu_appassets_zip}")
            message(FATAL_ERROR
                "PLAYGROUND_APP_ASSETS_ZIP does not exist: "
                "${_wiiu_appassets_zip}")
        endif()
        set(_wiiu_engine_content
            "${CMAKE_CURRENT_BINARY_DIR}/engine-content")
        set(_wiiu_engine_appassets
            "${_wiiu_engine_content}/AppAssets")
        set(_wiiu_engine_appassets_stamp
            "${CMAKE_CURRENT_BINARY_DIR}/AppAssets.expanded.stamp")
        set(_wiiu_engine_metadata
            "${_wiiu_engine_content}/AppAssets.metadata")
        set(_wiiu_engine_compiler
            "${_wiiu_engine_content}/glslcompiler.rpl")
        set(_wiiu_engine_ca_bundle
            "${_wiiu_engine_content}/cacert.pem")
        add_custom_command(
            OUTPUT "${_wiiu_engine_appassets_stamp}" "${_wiiu_engine_metadata}"
                   "${_wiiu_engine_compiler}" "${_wiiu_engine_ca_bundle}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "${_wiiu_engine_content}"
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/stage_wiiu_appassets.py"
                "${_wiiu_appassets_zip}" "${_wiiu_engine_appassets}"
                "${_wiiu_engine_appassets_stamp}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_wiiu_glsl_compiler}" "${_wiiu_engine_compiler}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_wiiu_ca_bundle}" "${_wiiu_engine_ca_bundle}"
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/generate_appassets_metadata.py"
                "${_wiiu_appassets_zip}" "${_wiiu_engine_metadata}"
            DEPENDS "${_wiiu_appassets_zip}" "${_wiiu_glsl_compiler}"
                "${_wiiu_ca_bundle}"
                "${PROJECT_SOURCE_DIR}/scripts/stage_wiiu_appassets.py"
                "${PROJECT_SOURCE_DIR}/scripts/generate_appassets_metadata.py"
            COMMENT "Expanding AppAssets into Wii U title content"
            VERBATIM)
        add_custom_target(playground-wiiu-engine-content
            DEPENDS "${_wiiu_engine_appassets_stamp}" "${_wiiu_engine_metadata}"
                    "${_wiiu_engine_compiler}" "${_wiiu_engine_ca_bundle}")
        set_target_properties(playground-wiiu-engine-content PROPERTIES
            DKP_ASSET_FOLDER "${_wiiu_engine_content}"
            DKP_ASSET_FILES
                "${_wiiu_engine_appassets_stamp};${_wiiu_engine_metadata};${_wiiu_engine_compiler};${_wiiu_engine_ca_bundle}")
        wut_create_wuhb(playground-wiiu-engine
            CONTENT playground-wiiu-engine-content
            NAME "PlaygroundOSS SIF"
            SHORTNAME "PlaygroundOSS"
            AUTHOR "PlaygroundOSS contributors")
    else()
        message(STATUS "PLAYGROUND_APP_ASSETS_ZIP is unset; the Wii U engine "
            "RPX will build, but the distributable WUHB is disabled")
    endif()
endif()
