find_package(PkgConfig REQUIRED)
option(PLAYGROUND_SWITCH_FILE_DIAGNOSTICS "Temporary bounded SD diagnostic log (adds SD permission)" OFF)
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
    add_executable(playground-switch-database-probe tools/switch_database_probe.cpp
        ../../runtime/tools/database_contract_probe.cpp
        ../../runtime/switch/src/SwitchStorage.cpp)
    target_compile_features(playground-switch-database-probe PRIVATE cxx_std_17)
    target_compile_definitions(playground-switch-database-probe PRIVATE
        PLAYGROUND_SWITCH_SAVE_SIZE=0x1000000
        PLAYGROUND_SWITCH_SAVE_JOURNAL_SIZE=0x800000
        PLAYGROUND_SWITCH_CACHE_SIZE=0x1000000
        PLAYGROUND_SWITCH_CACHE_JOURNAL_SIZE=0x800000)
    target_link_libraries(playground-switch-database-probe PRIVATE
        Playground::SwitchHost Playground::SwitchRuntime
        "-Wl,--start-group" playground_engine_runtime Playground::SwitchRuntime
        "-Wl,--end-group")
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/playground-switch-database-probe.nacp"
        COMMAND "${NX_NACPTOOL_EXE}" --create "PlaygroundOSS DB probe" "PlaygroundOSS"
            "1.0.0" "${CMAKE_CURRENT_BINARY_DIR}/playground-switch-database-probe.nacp"
            --titleid=0100F59153492000
        VERBATIM)
    nx_create_nro(playground-switch-database-probe-nro
        TARGET playground-switch-database-probe
        NACP "${CMAKE_CURRENT_BINARY_DIR}/playground-switch-database-probe.nacp")
    # A separate installed title is required to test Horizon SaveData. NRO/SD
    # success is not a substitute, and the probe must never access game saves.
    function(playground_database_probe_package)
        set(PLAYGROUND_SWITCH_TITLE_ID "0x0100F59153492000")
        set(probe_dir "${CMAKE_CURRENT_BINARY_DIR}/database-probe")
        file(MAKE_DIRECTORY "${probe_dir}")
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/packaging/application.npdm.json.in" probe_descriptor)
        string(REPLACE "\"fatal:u\"," "" probe_descriptor "${probe_descriptor}")
        string(REPLACE "\"svcOutputDebugString\": \"0x27\"," "" probe_descriptor "${probe_descriptor}")
        string(CONFIGURE "${probe_descriptor}" probe_descriptor @ONLY)
        file(GENERATE OUTPUT "${probe_dir}/application.json" CONTENT "${probe_descriptor}")
        add_custom_command(OUTPUT "${probe_dir}/main.npdm" "${probe_dir}/control.nacp"
            COMMAND "${DEVKITPRO}/tools/bin/npdmtool"
                "${probe_dir}/application.json" "${probe_dir}/main.npdm"
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/configure_switch_nacp.py"
                "${CMAKE_CURRENT_BINARY_DIR}/playground-switch-database-probe.nacp"
                "${probe_dir}/control.nacp" --title-id=0x0100F59153492000
                --save-size=0x1000000 --save-journal=0x800000
                --cache-size=0x1000000 --cache-journal=0x800000 --cache-max=0x2000000
            DEPENDS "${probe_dir}/application.json"
                "${CMAKE_CURRENT_BINARY_DIR}/playground-switch-database-probe.nacp"
                "${PROJECT_SOURCE_DIR}/scripts/configure_switch_nacp.py"
            VERBATIM)
        if(PLAYGROUND_SWITCH_ICON_JPEG)
            add_custom_target(playground-switch-database-probe-audit
                COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/audit_switch_npdm.py"
                    --elf "$<TARGET_FILE:playground-switch-database-probe>"
                    --json "${probe_dir}/application.json" --npdm "${probe_dir}/main.npdm"
                    --nm "${CMAKE_NM}"
                COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/audit_switch_nacp.py"
                    --nacp "${probe_dir}/control.nacp" --title-id=0x0100F59153492000
                    --save-size=0x1000000 --save-journal=0x800000
                    --cache-size=0x1000000 --cache-journal=0x800000 --cache-max=0x2000000
                DEPENDS playground-switch-database-probe "${probe_dir}/main.npdm"
                    "${probe_dir}/control.nacp"
                VERBATIM)
            add_custom_target(playground-switch-database-probe-stage
                COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/stage_switch_package.py"
                    --database-probe --elf "$<TARGET_FILE:playground-switch-database-probe>"
                    --npdm "${probe_dir}/main.npdm" --nacp "${probe_dir}/control.nacp"
                    --icon "${PLAYGROUND_SWITCH_ICON_JPEG}" --elf2nso "${NX_ELF2NSO_EXE}"
                    --output "${probe_dir}/package-stage" --title-id=0x0100F59153492000
                DEPENDS playground-switch-database-probe playground-switch-database-probe-audit "${probe_dir}/main.npdm"
                    "${probe_dir}/control.nacp"
                VERBATIM)
        endif()
    endfunction()
    playground_database_probe_package()
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
    set(_diagnostic_audit_args)
    if(PLAYGROUND_SWITCH_FILE_DIAGNOSTICS)
        target_compile_definitions(playground_switch_runtime PRIVATE PLAYGROUND_SWITCH_FILE_DIAGNOSTICS=1)
        file(READ "${_engine_npdm_json}" _diagnostic_descriptor)
        string(REPLACE "\"permissions\": \"0x21\"" "\"permissions\": \"0x200021\""
            _diagnostic_descriptor "${_diagnostic_descriptor}")
        string(REPLACE "\"svcSleepThread\": \"0x0b\"," "\"svcSleepThread\": \"0x0b\",\n        \"svcGetResourceLimitCurrentValue\": \"0x31\","
            _diagnostic_descriptor "${_diagnostic_descriptor}")
        file(WRITE "${_engine_npdm_json}" "${_diagnostic_descriptor}")
        list(APPEND _diagnostic_audit_args --allow-diagnostic-sd)
    endif()
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
            ${_diagnostic_audit_args}
            --npdm "${_engine_npdm}"
            --nm "${CMAKE_NM}"
        DEPENDS playground-switch-engine playground-switch-engine-npdm
                "${PROJECT_SOURCE_DIR}/scripts/audit_switch_npdm.py"
        VERBATIM)
    add_custom_target(playground-switch-engine-runtime-audit
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/scripts/audit_switch_runtime_contract.py"
            --elf "$<TARGET_FILE:playground-switch-engine>"
            --nm "${CMAKE_NM}"
        DEPENDS playground-switch-engine
                "${PROJECT_SOURCE_DIR}/scripts/audit_switch_runtime_contract.py"
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
        add_custom_target(playground-switch-engine-path-audit
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/audit_switch_asset_paths.py"
                --archive "${_romfs_archive}"
            DEPENDS playground-switch-engine-romfs
                    "${PROJECT_SOURCE_DIR}/scripts/audit_switch_asset_paths.py"
            VERBATIM)
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
                        playground-switch-engine-runtime-audit
                        playground-switch-engine-nacp-audit
                        playground-switch-engine-path-audit
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
