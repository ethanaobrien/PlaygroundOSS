add_library(playground_sdl_host STATIC
    src/DesktopHost.c
)
add_library(Playground::DesktopHost ALIAS playground_sdl_host)

target_include_directories(playground_sdl_host
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(playground_sdl_host PUBLIC c_std_11)
target_link_libraries(playground_sdl_host PUBLIC SDL3::SDL3)
set_target_properties(playground_sdl_host PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

add_executable(playground-desktop-host tools/desktop_host_main.c)
target_compile_features(playground-desktop-host PRIVATE c_std_11)
target_link_libraries(playground-desktop-host
    PRIVATE
        Playground::DesktopHost
        Playground::PlatformContract
)
if(WIN32)
    add_custom_command(TARGET playground-desktop-host POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3>"
            "$<TARGET_FILE_DIR:playground-desktop-host>"
        VERBATIM)
endif()

if(TARGET playground_engine_runtime)
    set(playground_sdl_engine_sources tools/sdl_engine_main.cpp)
    if(WIN32)
        # mt.exe cannot consume a manifest through a WSL UNC source path. Copy
        # it into the native build tree before Visual Studio embeds it.
        set(playground_windows_manifest
            "${CMAKE_CURRENT_BINARY_DIR}/windows-runtime.manifest")
        configure_file(windows-runtime.manifest
            "${playground_windows_manifest}" COPYONLY)
        list(APPEND playground_sdl_engine_sources
            src/WindowsAssetBootstrap.cpp
            "${playground_windows_manifest}"
        )
    endif()
    add_executable(playground-sdl-engine ${playground_sdl_engine_sources})
    target_compile_features(playground-sdl-engine PRIVATE cxx_std_17)
    target_include_directories(playground-sdl-engine PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(playground-sdl-engine
        PRIVATE
            Playground::DesktopHost
            $<LINK_LIBRARY:WHOLE_ARCHIVE,playground_engine_runtime>
            Playground::PlatformContract
    )
    if(WIN32)
        set_target_properties(playground-sdl-engine PROPERTIES WIN32_EXECUTABLE TRUE)
        target_link_options(playground-sdl-engine PRIVATE /ENTRY:mainCRTStartup)
        target_link_libraries(playground-sdl-engine PRIVATE ole32 shell32)
        add_custom_command(TARGET playground-sdl-engine POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3>"
                "$<TARGET_FILE_DIR:playground-sdl-engine>"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:unofficial::angle::libEGL>"
                "$<TARGET_FILE_DIR:playground-sdl-engine>"
            VERBATIM)

        add_executable(playground-windows-bootstrap-probe
            tools/windows_bootstrap_probe.cpp
            src/WindowsAssetBootstrap.cpp
            "${playground_windows_manifest}"
        )
        target_compile_features(playground-windows-bootstrap-probe PRIVATE cxx_std_17)
        target_include_directories(playground-windows-bootstrap-probe PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(playground-windows-bootstrap-probe PRIVATE
            playground_engine_runtime ole32 shell32)
    endif()
endif()
