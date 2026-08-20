include(FetchContent)

set(
    PLAYGROUND_SDL_PROVIDER
    auto
    CACHE STRING
    "SDL3 dependency provider: auto, system, or fetch"
)
set_property(CACHE PLAYGROUND_SDL_PROVIDER PROPERTY STRINGS auto system fetch)

set(_playground_sdl_providers auto system fetch)
if(NOT PLAYGROUND_SDL_PROVIDER IN_LIST _playground_sdl_providers)
    message(FATAL_ERROR "PLAYGROUND_SDL_PROVIDER must be auto, system, or fetch")
endif()

if(PLAYGROUND_SDL_PROVIDER STREQUAL system OR PLAYGROUND_SDL_PROVIDER STREQUAL auto)
    find_package(SDL3 3.4 CONFIG QUIET)
endif()

if(PLAYGROUND_SDL_PROVIDER STREQUAL system AND NOT TARGET SDL3::SDL3)
    message(FATAL_ERROR "PLAYGROUND_SDL_PROVIDER=system but SDL3 was not found")
endif()

if(NOT TARGET SDL3::SDL3)
    if(PLAYGROUND_TARGET_PLATFORM STREQUAL emscripten)
        set(SDL_SHARED OFF CACHE BOOL "Do not build SDL3 shared library" FORCE)
        set(SDL_STATIC ON CACHE BOOL "Build SDL3 static library" FORCE)
        set(SDL_PTHREADS ON CACHE BOOL "Build SDL3 with pthread support" FORCE)
    else()
        set(SDL_SHARED ON CACHE BOOL "Build SDL3 shared library" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "Do not build SDL3 static library" FORCE)
    endif()
    set(SDL_TESTS OFF CACHE BOOL "Do not build SDL3 tests" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "Do not build SDL3 test library" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "Do not build SDL3 examples" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "Do not install fetched SDL3" FORCE)
    set(
        SDL_X11_XSCRNSAVER
        OFF
        CACHE BOOL
        "Do not require the optional XScreenSaver development package"
        FORCE
    )
    FetchContent_Declare(
        SDL3
        URL https://github.com/libsdl-org/SDL/archive/f87239e71e42da91ca317a12eefb82cfbf3393eb.tar.gz
        URL_HASH SHA256=363ab3eb2225d700bf0b67bc16349f9949125c747ac50da3ba805c2d3a61b2a0
        DOWNLOAD_EXTRACT_TIMESTAMP OFF
    )
    FetchContent_MakeAvailable(SDL3)
endif()

if(NOT TARGET SDL3::SDL3)
    message(FATAL_ERROR "SDL3 did not provide the SDL3::SDL3 target")
endif()
