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

if(TARGET playground_engine_runtime)
    add_executable(playground-sdl-engine tools/sdl_engine_main.cpp)
    target_compile_features(playground-sdl-engine PRIVATE cxx_std_17)
    target_link_libraries(playground-sdl-engine
        PRIVATE
            Playground::DesktopHost
            $<LINK_LIBRARY:WHOLE_ARCHIVE,playground_engine_runtime>
            Playground::PlatformContract
    )
endif()
