find_package(PkgConfig REQUIRED)
pkg_check_modules(PLAYGROUND_SWITCH_EGL REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(PLAYGROUND_SWITCH_GLES2 REQUIRED IMPORTED_TARGET glesv2)

add_library(playground_switch_host STATIC src/SwitchHost.c)
add_library(Playground::SwitchHost ALIAS playground_switch_host)
target_compile_features(playground_switch_host PUBLIC c_std_11)
target_include_directories(playground_switch_host PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(playground_switch_host PUBLIC
    PkgConfig::PLAYGROUND_SWITCH_EGL
    PkgConfig::PLAYGROUND_SWITCH_GLES2)

add_executable(playground-switch-host tools/switch_host_main.c)
target_compile_features(playground-switch-host PRIVATE c_std_11)
set_target_properties(playground-switch-host PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(playground-switch-host PRIVATE
    Playground::SwitchHost
    Playground::PlatformContract)

nx_generate_nacp(
    OUTPUT playground-switch-host.nacp
    NAME "PlaygroundOSS host probe"
    AUTHOR "PlaygroundOSS contributors"
    VERSION "${PROJECT_VERSION}")
nx_create_nro(playground-switch-host-nro
    TARGET playground-switch-host
    NACP "${CMAKE_CURRENT_BINARY_DIR}/playground-switch-host.nacp")

if(PLAYGROUND_BUILD_ENGINE_RUNTIME)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(playground-switch-engine tools/switch_engine_main.cpp)
    target_compile_features(playground-switch-engine PRIVATE cxx_std_17)
    target_link_libraries(playground-switch-engine PRIVATE
        Playground::SwitchHost
        Playground::SwitchRuntime
        Playground::PlatformContract
        "-Wl,--whole-archive"
        playground_engine_runtime
        "-Wl,--no-whole-archive")

    # nacptool consumes exactly sixteen hexadecimal digits.  Passing C/C++'s
    # conventional 0x prefix makes the older devkitPro tool consume the first
    # sixteen characters and silently truncate the final byte of the title ID.
    string(REGEX REPLACE "^0[xX]" "" _nacptool_title_id
        "${PLAYGROUND_SWITCH_TITLE_ID}")
    string(LENGTH "${_nacptool_title_id}" _nacptool_title_id_length)
    if(NOT _nacptool_title_id MATCHES "^[0-9A-Fa-f]+$"
       OR NOT _nacptool_title_id_length EQUAL 16)
        message(FATAL_ERROR
            "PLAYGROUND_SWITCH_TITLE_ID must contain exactly 16 hex digits")
    endif()

    set(_base_nacp "${CMAKE_CURRENT_BINARY_DIR}/playground-switch-engine.base.nacp")
    set(_engine_nacp "${CMAKE_CURRENT_BINARY_DIR}/playground-switch-engine.nacp")
    set(_engine_npdm_json
        "${CMAKE_CURRENT_BINARY_DIR}/playground-switch-engine.npdm.json")
    set(_engine_npdm
        "${CMAKE_CURRENT_BINARY_DIR}/playground-switch-engine.npdm")
    configure_file(packaging/application.npdm.json.in "${_engine_npdm_json}" @ONLY)
    add_custom_command(OUTPUT "${_engine_npdm}"
        COMMAND "${DEVKITPRO}/tools/bin/npdmtool"
            "${_engine_npdm_json}" "${_engine_npdm}"
        DEPENDS "${_engine_npdm_json}"
        COMMENT "Generating least-privilege Switch process descriptor"
        VERBATIM)
    add_custom_target(playground-switch-engine-npdm
        DEPENDS "${_engine_npdm}")
    add_custom_target(playground-switch-engine-npdm-audit
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/scripts/audit_switch_npdm.py"
            --elf "$<TARGET_FILE:playground-switch-engine>"
            --json "${_engine_npdm_json}"
            --npdm "${_engine_npdm}"
            --nm "${CMAKE_NM}"
        DEPENDS playground-switch-engine playground-switch-engine-npdm
                "${PROJECT_SOURCE_DIR}/scripts/audit_switch_npdm.py"
        VERBATIM)
    add_custom_command(OUTPUT "${_base_nacp}"
        COMMAND "${NX_NACPTOOL_EXE}" --create
            "PlaygroundOSS SIF" "PlaygroundOSS contributors"
            "${PROJECT_VERSION}" "${_base_nacp}"
            "--titleid=${_nacptool_title_id}"
        VERBATIM)
    add_custom_command(OUTPUT "${_engine_nacp}"
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/scripts/configure_switch_nacp.py"
            "${_base_nacp}" "${_engine_nacp}"
            "--title-id=${PLAYGROUND_SWITCH_TITLE_ID}"
            "--save-size=${PLAYGROUND_SWITCH_SAVE_SIZE}"
            "--save-journal=${PLAYGROUND_SWITCH_SAVE_JOURNAL_SIZE}"
            "--cache-size=${PLAYGROUND_SWITCH_CACHE_SIZE}"
            "--cache-journal=${PLAYGROUND_SWITCH_CACHE_JOURNAL_SIZE}"
            "--cache-max=${PLAYGROUND_SWITCH_CACHE_MAX_SIZE}"
        DEPENDS "${_base_nacp}"
                "${PROJECT_SOURCE_DIR}/scripts/configure_switch_nacp.py"
        VERBATIM)
    add_custom_target(playground-switch-engine-nacp-audit
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/scripts/audit_switch_nacp.py"
            --nacp "${_engine_nacp}"
            --title-id "${PLAYGROUND_SWITCH_TITLE_ID}"
            --save-size "${PLAYGROUND_SWITCH_SAVE_SIZE}"
            --save-journal "${PLAYGROUND_SWITCH_SAVE_JOURNAL_SIZE}"
            --cache-size "${PLAYGROUND_SWITCH_CACHE_SIZE}"
            --cache-journal "${PLAYGROUND_SWITCH_CACHE_JOURNAL_SIZE}"
            --cache-max "${PLAYGROUND_SWITCH_CACHE_MAX_SIZE}"
        DEPENDS "${_engine_nacp}"
                "${PROJECT_SOURCE_DIR}/scripts/audit_switch_nacp.py"
                "${PROJECT_SOURCE_DIR}/scripts/configure_switch_nacp.py"
        VERBATIM)

    set(_engine_romfs "${CMAKE_CURRENT_BINARY_DIR}/playground-switch-romfs")
    if(PLAYGROUND_APP_ASSETS_ZIP)
        get_filename_component(_appassets_zip "${PLAYGROUND_APP_ASSETS_ZIP}"
            ABSOLUTE)
        if(NOT EXISTS "${_appassets_zip}")
            message(FATAL_ERROR
                "PLAYGROUND_APP_ASSETS_ZIP does not exist: ${_appassets_zip}")
        endif()
        set(_romfs_archive "${_engine_romfs}/AppAssets.zip")
        set(_romfs_metadata "${_engine_romfs}/AppAssets.metadata")
        add_custom_command(OUTPUT "${_romfs_archive}" "${_romfs_metadata}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_engine_romfs}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_appassets_zip}" "${_romfs_archive}"
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/generate_appassets_metadata.py"
                "${_appassets_zip}" "${_romfs_metadata}"
            DEPENDS "${_appassets_zip}"
                    "${PROJECT_SOURCE_DIR}/scripts/generate_appassets_metadata.py"
            COMMENT "Staging unchanged AppAssets.zip for Switch RomFS"
            VERBATIM)
        add_custom_target(playground-switch-engine-romfs
            DEPENDS "${_romfs_archive}" "${_romfs_metadata}")
        set_target_properties(playground-switch-engine-romfs PROPERTIES
            DKP_ASSET_FOLDER "${_engine_romfs}"
            DKP_ASSET_FILES "${_romfs_archive};${_romfs_metadata}")
        target_compile_definitions(playground-switch-engine PRIVATE
            PLAYGROUND_SWITCH_BUNDLED_ASSETS=1)
        nx_create_nro(playground-switch-engine-nro
            TARGET playground-switch-engine
            NACP "${_engine_nacp}"
            ROMFS playground-switch-engine-romfs)
        if(PLAYGROUND_SWITCH_ICON_JPEG)
            get_filename_component(_switch_icon "${PLAYGROUND_SWITCH_ICON_JPEG}"
                ABSOLUTE)
            set(_package_stage "${CMAKE_CURRENT_BINARY_DIR}/package-stage")
            add_custom_target(playground-switch-package-stage
                COMMAND "${Python3_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/scripts/stage_switch_package.py"
                    --elf "$<TARGET_FILE:playground-switch-engine>"
                    --npdm "${_engine_npdm}"
                    --nacp "${_engine_nacp}"
                    --archive "${_romfs_archive}"
                    --metadata "${_romfs_metadata}"
                    --icon "${_switch_icon}"
                    --elf2nso "${NX_ELF2NSO_EXE}"
                    --output "${_package_stage}"
                    --title-id "${PLAYGROUND_SWITCH_TITLE_ID}"
                DEPENDS playground-switch-engine playground-switch-engine-npdm
                        playground-switch-engine-nacp-audit
                        playground-switch-engine-romfs "${_engine_nacp}"
                        "${_switch_icon}"
                        "${PROJECT_SOURCE_DIR}/scripts/stage_switch_package.py"
                VERBATIM)
        else()
            message(STATUS "PLAYGROUND_SWITCH_ICON_JPEG is unset; the NRO will "
                "build, but installed-package staging is deliberately disabled")
        endif()
    else()
        message(STATUS
            "Switch engine NRO is a development build; provide "
            "-DPLAYGROUND_APP_ASSETS_ZIP=/path/AppAssets.zip for bundled RomFS")
        nx_create_nro(playground-switch-engine-nro
            TARGET playground-switch-engine
            NACP "${_engine_nacp}")
    endif()
endif()
